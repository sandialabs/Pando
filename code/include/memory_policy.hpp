#pragma once

/**
 * Memory-aware early stage close.
 *
 * Filter output is held in memory until the stage closes: in SeqDB as staged
 * entries/tags/values, and in ParDB as messages queued to (and staged by) the
 * owning agents. Only the stage close moves it into the (file-backed) map.
 * A MemoryPolicy bounds that by ending a SINGLE_ENTRY pass early, keeping the
 * scan cursor so the next pass continues where this one stopped:
 *
 *  - Byte budget: at the start of each pass, the budget is a fraction of the
 *    node's memory headroom (MemAvailable above the reserve the limit leaves),
 *    split over the agents on the node, and/or a fixed number of bytes.
 *    "The node's memory" is limited by the process's cgroup when it has a
 *    memory limit (read_effective_meminfo): under Slurm, the job's --mem is
 *    what the job is killed at, and it can be well below the node's memory. The
 *    pass stops once its filters have emitted that many bytes of output.
 *  - Pressure: the pass also stops if MemAvailable drops below the reserve
 *    while it runs (e.g. because of other agents or other processes).
 *  - Both apply only once at least one filter has run in the pass, so every
 *    pass makes progress even if memory is already high when it starts: with
 *    little headroom the budget is ~0 and each pass runs one entry's filters,
 *    closes the stage (moving the output to the map), and the next continues.
 *
 * In ParDB, an agent that stops for either memory reason also asks every
 * agent to close early (CLOSE_EARLY), since the others' output fills its
 * queues until the stage closes.
 *
 * Kept free of DB and ZeroMQ dependencies so it can be tested on its own.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "process_stats.hpp"

namespace pando {

/** @brief System memory, in bytes */
struct MemInfo {
    /** false if the values could not be read */
    bool ok = false;
    uint64_t total_bytes = 0;
    /** The kernel's estimate of memory available without swapping
     * (MemAvailable), which counts reclaimable page cache, such as the pages
     * of the file-backed map, as available */
    uint64_t available_bytes = 0;
};

/** @brief Parse the format of /proc/meminfo ("MemTotal:  123 kB" lines) */
inline MemInfo parse_meminfo(std::istream& in) {
    MemInfo m;
    bool have_total = false, have_avail = false;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls { line };
        std::string key, unit;
        uint64_t val = 0;
        if (!(ls >> key >> val)) continue;
        ls >> unit;
        uint64_t mult = (unit == "kB") ? 1024 : 1;
        if (key == "MemTotal:") { m.total_bytes = val * mult; have_total = true; }
        else if (key == "MemAvailable:") { m.available_bytes = val * mult; have_avail = true; }
        if (have_total && have_avail) break;
    }
    m.ok = have_total && have_avail && m.total_bytes > 0;
    return m;
}

/** @brief Read the system's memory from /proc/meminfo (Linux) */
inline MemInfo read_meminfo(const std::string& path = "/proc/meminfo") {
    std::ifstream in { path };
    if (!in) return MemInfo{};
    return parse_meminfo(in);
}

/** @brief A memory limit from this process's cgroup (e.g. a Slurm job's --mem) */
struct CgroupMemory {
    /** false if no limit was found (no cgroup, files not readable, or unlimited) */
    bool ok = false;
    uint64_t limit_bytes = 0;
    /** Memory charged to the cgroup with the limit, minus its inactive file
     * pages (which the kernel reclaims first), so it compares with
     * MemAvailable, which also counts reclaimable page cache as available */
    uint64_t used_bytes = 0;
};

/** @brief Read the first number in a file; "max" (cgroup v2) means unlimited
 * @return false if the file cannot be read */
inline bool read_cgroup_value_(const std::string& path, uint64_t& value, bool& unlimited) {
    std::ifstream in { path };
    std::string tok;
    if (!(in >> tok)) return false;
    unlimited = (tok == "max");
    if (unlimited) return true;
    for (char c : tok) if (c < '0' || c > '9') return false;
    value = std::strtoull(tok.c_str(), nullptr, 10);
    // cgroup v1 writes "no limit" as a huge page-aligned number
    if (value >= (1ull << 60)) unlimited = true;
    return true;
}

/** @brief A "key value" entry of a memory.stat file; 0 if absent */
inline uint64_t read_cgroup_stat_(const std::string& path, const std::string& key) {
    std::ifstream in { path };
    std::string k;
    uint64_t v;
    while (in >> k >> v)
        if (k == key) return v;
    return 0;
}

/** @brief Find the tightest memory limit on the path from a cgroup up to the
 * root of its hierarchy
 *
 * Limits are often set on an ancestor rather than the process's own cgroup
 * (Slurm sets --mem on the job or step, the process is in a task cgroup), so
 * every level is checked, and usage is taken from the cgroup that has the
 * tightest limit.
 *
 * @param proc_cgroup the contents of /proc/self/cgroup
 * @param root where the cgroup file systems are mounted
 */
inline CgroupMemory find_cgroup_memory(const std::string& proc_cgroup, const std::string& root = "/sys/fs/cgroup") {
    // Lines are "hierarchy-id:controllers:path"
    std::string v2_path, v1_path;
    bool have_v2 = false, have_v1 = false;
    std::istringstream lines { proc_cgroup };
    std::string line;
    while (std::getline(lines, line)) {
        size_t c1 = line.find(':');
        size_t c2 = c1 == std::string::npos ? std::string::npos : line.find(':', c1 + 1);
        if (c2 == std::string::npos) continue;
        std::string id = line.substr(0, c1), ctrls = line.substr(c1 + 1, c2 - c1 - 1), path = line.substr(c2 + 1);
        if (id == "0" && ctrls.empty()) { v2_path = path; have_v2 = true; }
        else {
            std::istringstream cs { ctrls };
            std::string ctrl;
            while (std::getline(cs, ctrl, ','))
                if (ctrl == "memory") { v1_path = path; have_v1 = true; }
        }
    }

    auto walk = [](std::string base, std::string path, const char* limit_file, const char* usage_file,
                   const char* inactive_key, const char* inactive_key_alt) {
        CgroupMemory best;
        while (true) {
            std::string dir = base + (path == "/" ? "" : path);
            uint64_t limit = 0;
            bool unlimited = false;
            if (read_cgroup_value_(dir + "/" + limit_file, limit, unlimited) && !unlimited &&
                    (!best.ok || limit < best.limit_bytes)) {
                uint64_t usage = 0;
                bool usage_unlimited = false;
                if (read_cgroup_value_(dir + "/" + usage_file, usage, usage_unlimited)) {
                    uint64_t inactive = read_cgroup_stat_(dir + "/memory.stat", inactive_key);
                    if (inactive == 0 && inactive_key_alt != nullptr)
                        inactive = read_cgroup_stat_(dir + "/memory.stat", inactive_key_alt);
                    best.ok = true;
                    best.limit_bytes = limit;
                    best.used_bytes = usage > inactive ? usage - inactive : 0;
                }
            }
            if (path.empty() || path == "/") break;
            size_t slash = path.find_last_of('/');
            path = slash == 0 || slash == std::string::npos ? "/" : path.substr(0, slash);
        }
        return best;
    };

    if (have_v2) {
        CgroupMemory m = walk(root, v2_path, "memory.max", "memory.current", "inactive_file", nullptr);
        if (m.ok) return m;
    }
    if (have_v1)
        return walk(root + "/memory", v1_path, "memory.limit_in_bytes", "memory.usage_in_bytes",
                    "total_inactive_file", "inactive_file");
    return CgroupMemory{};
}

/** @brief Memory as the node's and the cgroup's limits allow together: the
 * smaller total, and the smaller amount available */
inline MemInfo apply_cgroup_limit(const MemInfo& node, const CgroupMemory& cg) {
    if (!cg.ok) return node;
    MemInfo m;
    m.ok = true;
    uint64_t cg_avail = cg.limit_bytes > cg.used_bytes ? cg.limit_bytes - cg.used_bytes : 0;
    m.total_bytes = node.ok ? std::min(node.total_bytes, cg.limit_bytes) : cg.limit_bytes;
    m.available_bytes = node.ok ? std::min(node.available_bytes, cg_avail) : cg_avail;
    if (m.available_bytes > m.total_bytes) m.available_bytes = m.total_bytes;
    return m;
}

/** @brief This process's memory: the node's (/proc/meminfo), limited by its
 * cgroup's memory limit if it has one (under Slurm, the job's --mem, which is
 * what the job is killed at) */
inline MemInfo read_effective_meminfo(const std::string& meminfo_path = "/proc/meminfo",
                                      const std::string& proc_cgroup_path = "/proc/self/cgroup",
                                      const std::string& cgroup_root = "/sys/fs/cgroup") {
    MemInfo node = read_meminfo(meminfo_path);
    std::ifstream in { proc_cgroup_path };
    if (!in) return node;
    std::stringstream text;
    text << in.rdbuf();
    return apply_cgroup_limit(node, find_cgroup_memory(text.str(), cgroup_root));
}

/** @brief When to end a processing pass early to bound memory (see above)
 *
 * Enabled by default: node memory is kept below 90%, and each pass may emit
 * a quarter of the headroom (split over the agents on the node). Use
 * disabled() to only stop passes at max_filters_per_stage or the end of
 * their lap.
 */
struct MemoryPolicy {
    /** Keep node memory use below this percent of MemTotal; 0 disables the
     * headroom-based budget and the pressure check. 90% leaves room for the
     * OS, page cache, and the overshoot between the checks and the other
     * agents noticing a CLOSE_EARLY. */
    double limit_pct = 90;
    /** A fixed byte budget per pass; 0 for none. With limit_pct also set, the
     * smaller of the two budgets applies. */
    uint64_t pass_budget_bytes = 0;
    /** Fraction of the headroom the agents on a node may emit per pass,
     * together. The budget counts serialized bytes, but until the stage
     * closes the output is held as queued messages (on the sender and the
     * receiver) and then as staged entries, which take a few times their
     * serialized size. A quarter keeps that within the headroom. */
    double budget_fraction = 0.25;
    /** Agents sharing the node's memory; the headroom is split between them.
     * 0 (the default) works it out: see SeqDB::set_agents_per_node_source
     * (ParDB uses Slurm's tasks per node, else the agents in the mesh with
     * this agent's IP address), else 1. */
    uint64_t agents_per_node = 0;
    /** How often to re-read the system memory (and check for requests from
     * other agents) while filters run. Zero checks after every entry. */
    std::chrono::steady_clock::duration check_interval = std::chrono::milliseconds(10);

    /** @brief A policy that never stops a pass for memory (requests from
     * other agents are still honored) */
    static MemoryPolicy disabled() {
        MemoryPolicy p;
        p.limit_pct = 0;
        p.pass_budget_bytes = 0;
        return p;
    }

    /** @brief Whether this agent limits its own passes */
    bool enabled() const { return limit_pct > 0 || pass_budget_bytes > 0; }

    /** @brief Throw invalid_argument if any setting is out of range */
    void validate() const {
        if (!(limit_pct >= 0 && limit_pct <= 100))
            throw std::invalid_argument("memory limit_pct must be in [0, 100]");
        if (!(budget_fraction > 0 && budget_fraction <= 1))
            throw std::invalid_argument("memory budget_fraction must be in (0, 1]");
        if (check_interval < std::chrono::steady_clock::duration::zero())
            throw std::invalid_argument("memory check_interval must not be negative");
    }
};

/** @brief The memory limits for one pass, set when it starts */
struct PassBudget {
    /** The pass stops if MemAvailable drops below this many bytes */
    uint64_t reserve_bytes = 0;
    /** The pass stops once it has emitted this many bytes */
    uint64_t budget_bytes = NO_BYTE_BUDGET;
};

/** @brief The headroom-based limits for a pass (policy.limit_pct > 0)
 *
 * reserve = (100 - limit_pct)% of MemTotal
 * budget  = budget_fraction * max(0, MemAvailable - reserve) / agents
 *
 * The budget is 0 when memory is already above the limit; the pass then
 * stops after its first filter run.
 *
 * @param agents the agents sharing the node's memory (resolved from
 *        policy.agents_per_node by the caller; 0 is treated as 1)
 */
inline PassBudget compute_pass_budget(const MemoryPolicy& policy, const MemInfo& mem, uint64_t agents) {
    if (agents == 0) agents = 1;
    PassBudget b;
    double reserve = (100.0 - policy.limit_pct) / 100.0 * (double)mem.total_bytes;
    b.reserve_bytes = reserve <= 0 ? 0 : (uint64_t)reserve;
    uint64_t headroom = mem.available_bytes > b.reserve_bytes ? mem.available_bytes - b.reserve_bytes : 0;
    b.budget_bytes = (uint64_t)(policy.budget_fraction * (double)headroom / (double)agents);
    return b;
}

/** @brief Parse a positive base-10 integer filling all of [s, end) */
inline bool parse_positive_count_(const char* s, const char* end, uint64_t& out) {
    if (s == end) return false;
    uint64_t v = 0;
    for (const char* p = s; p != end; ++p) {
        if (*p < '0' || *p > '9') return false;
        if (v > (UINT64_MAX - (uint64_t)(*p - '0')) / 10) return false;
        v = v*10 + (uint64_t)(*p - '0');
    }
    if (v == 0) return false;
    out = v;
    return true;
}

/** @brief Tasks per node from Slurm's environment; 0 if unknown
 *
 * SLURM_NTASKS_PER_NODE (set by --ntasks-per-node) is used if it is a
 * positive integer. Otherwise SLURM_TASKS_PER_NODE (e.g. "16" or "16(x4)")
 * is used if every node has the same count; a mixed list such as "2(x3),1"
 * does not say which count is this node's, so it gives 0.
 *
 * Takes the values (or nullptr if unset) so it can be tested; see
 * slurm_tasks_per_node() for the environment.
 */
inline uint64_t parse_slurm_tasks_per_node(const char* ntasks_per_node, const char* tasks_per_node) {
    uint64_t n = 0;
    if (ntasks_per_node != nullptr &&
            parse_positive_count_(ntasks_per_node, ntasks_per_node + std::char_traits<char>::length(ntasks_per_node), n))
        return n;
    if (tasks_per_node != nullptr) {
        std::string s { tasks_per_node };
        if (s.find(',') != std::string::npos) return 0;
        size_t paren = s.find('(');
        // "16" or "16(x4)"
        if (paren != std::string::npos) {
            if (s.size() < paren + 4 || s.compare(paren, 2, "(x") != 0 || s.back() != ')') return 0;
            uint64_t reps = 0;
            if (!parse_positive_count_(s.data() + paren + 2, s.data() + s.size() - 1, reps)) return 0;
        } else {
            paren = s.size();
        }
        if (parse_positive_count_(s.data(), s.data() + paren, n)) return n;
    }
    return 0;
}

/** @brief Tasks per node from this process's Slurm environment; 0 if unknown */
inline uint64_t slurm_tasks_per_node() {
    return parse_slurm_tasks_per_node(std::getenv("SLURM_NTASKS_PER_NODE"), std::getenv("SLURM_TASKS_PER_NODE"));
}

/** @brief How many of the agents have the same IP address as self
 *
 * Agent addresses are an IPv4 address in the low 32 bits and a local number
 * above them (see elga::ZMQAddress::serialize), and agents on one host share
 * the address. (Hostnames are not used: addresses are always parsed as IPv4.)
 * Counts self if it is in the list.
 */
template <class Agents>
inline uint64_t count_agents_sharing_ip(const Agents& agents, addr_t self) {
    uint64_t n = 0;
    for (addr_t a : agents)
        if ((uint32_t)a == (uint32_t)self) ++n;
    return n;
}

/** @brief Payload of a CLOSE_EARLY message */
struct CloseEarlyRequest {
    /** The agent asking */
    addr_t agent = 0;
    /** The pass to close early (ProcessStats::pass), which every agent
     * advances together; a request for any other pass is ignored */
    uint64_t pass = 0;
    /** Why the agent stopped (STOP_MEM_BUDGET or STOP_MEM_PRESSURE) */
    uint8_t reason = STOP_NONE;

    void serialize(StatsWriter& w) const {
        w.put(agent);
        w.put(pass);
        w.put(reason);
    }

    /** @brief Parse a payload (after the message type); false if malformed */
    bool deserialize(const char* data, const char* end) {
        StatsReader r { data, end };
        agent = r.get<addr_t>();
        pass = r.get<uint64_t>();
        reason = r.get<uint8_t>();
        return r.ok() && r.done();
    }
};

}
