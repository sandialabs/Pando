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
 *    split over the agents on the node, and/or a fixed number of bytes. The
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
