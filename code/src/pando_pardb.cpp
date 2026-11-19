#include <iostream>
#include <exception>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "address.hpp"
#include "par_db.hpp"

#include "util.hpp"

using namespace std;

using namespace pando;

volatile sig_atomic_t *g_shutdown = nullptr;

void si_handle(int) {
    cerr << "[Pando] Ctrl+C caught, shutting down..." << endl;
    if (g_shutdown == nullptr) throw runtime_error("Unable to find shutdown flag");
    *g_shutdown = true;
}
void set_handler() {
    struct sigaction si_handler;
    si_handler.sa_handler = si_handle;
    sigemptyset(&si_handler.sa_mask);
    si_handler.sa_flags = SA_RESTART;
    sigaction(SIGINT, &si_handler, NULL);
}

/** @brief Parse a positive base-10 integer (no sign, no trailing text) */
static bool parse_positive_u64(const char* val, unsigned long long& out) {
    char* end = nullptr;
    errno = 0;
    unsigned long long n = strtoull(val, &end, 10);
    if (*val == '\0' || *val == '-' || *val == '+' || *end != '\0' || errno != 0 || n == 0) return false;
    out = n;
    return true;
}

/** @brief Parse a number in (lo, hi], or [lo, hi] if lo_inclusive (no trailing text) */
static bool parse_double_in(const char* val, double lo, double hi, double& out, bool lo_inclusive=false) {
    char* end = nullptr;
    errno = 0;
    double d = strtod(val, &end);
    if (*val == '\0' || *end != '\0' || errno != 0) return false;
    if (!((lo_inclusive ? d >= lo : d > lo) && d <= hi)) return false;
    out = d;
    return true;
}

int main_(int argc, char **argv) {
    cerr << "[Pando] [INFO] Loading..." << endl;

    if (argc < 2) {
        cerr << "Usage: pando_pardb bind-addr [seed-addr] [-M<mem in GB>] [--skip-group-filters] [--max-filters-per-stage=N]\n"
            "                   [--write-batch-bytes=N] [--scan-block-keys=N] [--continuous-processing=on|off]\n"
            "                   [--mem-limit-pct=P] [--mem-pass-budget-mb=N] [--mem-budget-fraction=F]\n"
            "                   [--agents-per-node=N] [--intern-tags=on|off]\n"
            "\n"
            "Parameters:\n"
            "  bind-addr : the address to bind this specific DB agent to\n"
            "  seed-addr : an address in a mesh to join\n"
            "  -M<mem> : memory in GB, defaults to 16 (e.g., -M8 would allocate 8 GB)\n"
            "  --skip-group-filters : skip processing of group filters\n"
            "  --max-filters-per-stage=N : run at most N filters per processing pass before\n"
            "                              closing the stage (bounds memory; default unlimited)\n"
            "  --write-batch-bytes=N : while filters run, send each agent's writes in messages\n"
            "                          of about N bytes instead of one message per write\n"
            "                          (default 262144; 0 = one message per write)\n"
            "  --scan-block-keys=N : keys the filter scan fetches from the map per request\n"
            "                          (default 4096; each reply is also capped at ~16 MB)\n"
            "  --continuous-processing=on|off : how a process request ends. on (default): keep\n"
            "                          sweeping over the entries until a whole sweep runs no\n"
            "                          filter, so filters that make other entries eligible are\n"
            "                          followed up. off: process each entry once per request,\n"
            "                          then stop (a sweep can still span several passes);\n"
            "                          entries created or made eligible during it are left for\n"
            "                          the next request. Use off only if no filter's writes can\n"
            "                          make an entry eligible for a filter in the same request.\n"
            "  --intern-tags=on|off : store each distinct tag once per process and have entries\n"
            "                          refer to it by id (default on). Turn off only if entries\n"
            "                          get unique tags, which would make the tag table grow\n"
            "                          with every entry.\n"
            "\n"
            "Memory limits:\n"
            "  Filter output is held in memory until the stage closes. A processing pass ends\n"
            "  early (and asks every agent to do the same) so the stage closes and the output\n"
            "  is moved to the map before memory runs out.\n"
            "  --mem-limit-pct=P : keep memory use below P% (0 <= P <= 100, default 90; 0\n"
            "                      turns this off) of the node's memory, or of this job's\n"
            "                      cgroup memory limit (e.g. Slurm's --mem) if lower.\n"
            "                      Each pass may emit a share of the\n"
            "                      headroom left when it starts, and stops if memory use goes\n"
            "                      over P% while it runs\n"
            "  --mem-pass-budget-mb=N : also emit at most N MiB of output per pass (the\n"
            "                           smaller budget applies; default none)\n"
            "  --mem-budget-fraction=F : share of the headroom the agents on a node may emit\n"
            "                            per pass together (0 < F <= 1, default 0.25)\n"

            "  --agents-per-node=N : agents sharing this node's memory (default: Slurm's tasks\n"
            "                        per node, else the agents in the mesh with this agent's\n"
            "                        IP address)\n"
            "\n"
            "Addresses are of the form: IPv4-string,ID\n"
            "  IPv4-string : a period separated IP address, e.g., 1.2.3.4\n"
            "  ID : a small integer between 0 and 199\n"
            "Addresses should be globally unique." << endl;
        return 1;
    }

    elga::ZMQAddress bind_addr = get_zmq_addr(argv[1]);
    string seed_addr;
    size_t sz = 16ull*(1ull<<30);
    bool skip_group_filters = false;
    size_t max_filters_per_stage = 0;  // 0 = SeqDB default (unlimited)
    MemoryPolicy mem_policy;    // enabled by default (see memory_policy.hpp)

    const string max_filters_flag = "--max-filters-per-stage=";
    size_t write_batch_bytes = ParDB::DEFAULT_WRITE_BATCH_BYTES;
    const string write_batch_flag = "--write-batch-bytes=";
    size_t scan_block_keys = 0;  // 0 = SeqDB default
    const string scan_block_flag = "--scan-block-keys=";
    bool continuous_processing = true;
    const string continuous_flag = "--continuous-processing=";
    const string mem_limit_flag = "--mem-limit-pct=";
    const string mem_budget_flag = "--mem-pass-budget-mb=";
    const string mem_fraction_flag = "--mem-budget-fraction=";
    bool intern_tags = true;
    const string intern_flag = "--intern-tags=";
    const string agents_flag = "--agents-per-node=";
    auto flag_val = [](const string& arg, const string& flag) -> const char* {
        return arg.rfind(flag, 0) == 0 ? arg.c_str() + flag.size() : nullptr;
    };
    auto bad = [](const string& flag, const char* val, const char* what) {
        cerr << "[Pando] [ERROR] " << flag << " needs " << what << ", got '" << val << "'" << endl;
        return 1;
    };

    for (int idx = 2; idx < argc; ++idx) {
        string arg { argv[idx] };
        const char* val = nullptr;
        unsigned long long n = 0;
        if (argv[idx][0] == '-' && argv[idx][1] == 'M') {
            sz = (1ull<<30)*strtoul(&(argv[idx][2]), NULL, 10);
        } else if (arg == "--skip-group-filters") {
            skip_group_filters = true;
        } else if ((val = flag_val(arg, max_filters_flag))) {
            if (!parse_positive_u64(val, n)) return bad(max_filters_flag, val, "a positive integer");
            max_filters_per_stage = n;
        } else if ((val = flag_val(arg, write_batch_flag))) {
            // 0 is allowed: it turns batching off
            if (string(val) == "0") write_batch_bytes = 0;
            else if (!parse_positive_u64(val, n)) return bad(write_batch_flag, val, "a non-negative integer");
            else write_batch_bytes = n;
        } else if ((val = flag_val(arg, scan_block_flag))) {
            if (!parse_positive_u64(val, n)) return bad(scan_block_flag, val, "a positive integer");
            scan_block_keys = n;
        } else if ((val = flag_val(arg, continuous_flag))) {
            if (string(val) == "on") continuous_processing = true;
            else if (string(val) == "off") continuous_processing = false;
            else return bad(continuous_flag, val, "on or off");
        } else if ((val = flag_val(arg, mem_limit_flag))) {
            if (!parse_double_in(val, 0, 100, mem_policy.limit_pct, true)) return bad(mem_limit_flag, val, "a percent in [0, 100]");
        } else if ((val = flag_val(arg, mem_budget_flag))) {
            if (!parse_positive_u64(val, n) || n > UINT64_MAX / (1ull<<20)) return bad(mem_budget_flag, val, "a positive integer");
            mem_policy.pass_budget_bytes = n * (1ull<<20);
        } else if ((val = flag_val(arg, mem_fraction_flag))) {
            if (!parse_double_in(val, 0, 1, mem_policy.budget_fraction)) return bad(mem_fraction_flag, val, "a number in (0, 1]");
        } else if ((val = flag_val(arg, agents_flag))) {
            if (!parse_positive_u64(val, n)) return bad(agents_flag, val, "a positive integer");
            mem_policy.agents_per_node = n;
        } else if ((val = flag_val(arg, intern_flag))) {
            if (string(val) == "on") intern_tags = true;
            else if (string(val) == "off") intern_tags = false;
            else return bad(intern_flag, val, "on or off");
        } else if (arg.rfind("--", 0) == 0) {
            cerr << "[Pando] [ERROR] unknown option '" << arg << "'" << endl;
            return 1;
        } else {
            if (seed_addr.size() != 0) throw runtime_error("Multiple seed addrs given");
            seed_addr.assign(argv[idx]);
        }
    }

    cerr << "[Pando] [DEBUG] Bind addr=" << bind_addr.get_conn_str(bind_addr, REQUEST) << " memory=" << sz
         << " max_filters_per_stage=" << (max_filters_per_stage ? to_string(max_filters_per_stage) : string("unlimited"))
         << " write_batch_bytes=" << (write_batch_bytes ? to_string(write_batch_bytes) : string("off"))
         << " scan_block_keys=" << (scan_block_keys ? to_string(scan_block_keys) : string("default"))
         << " continuous_processing=" << (continuous_processing ? "on" : "off")
         << " intern_tags=" << (intern_tags ? "on" : "off") << endl;
    if (mem_policy.enabled()) {
        cerr << "[Pando] [DEBUG] Memory limits: limit_pct=" << (mem_policy.limit_pct > 0 ? to_string(mem_policy.limit_pct) : string("off"))
             << " pass_budget_bytes=" << (mem_policy.pass_budget_bytes ? to_string(mem_policy.pass_budget_bytes) : string("off"))
             << " budget_fraction=" << mem_policy.budget_fraction
             << " agents_per_node=" << (mem_policy.agents_per_node ? to_string(mem_policy.agents_per_node) : string("auto")) << endl;
        MemInfo node = read_meminfo(), effective = read_effective_meminfo();
        if (effective.ok)
            cerr << "[Pando] [DEBUG] Memory: " << (effective.total_bytes >> 20) << " MiB total, "
                 << (effective.available_bytes >> 20) << " MiB available ("
                 << (node.ok && effective.total_bytes < node.total_bytes ? "limited by this job's cgroup" : "the node's")
                 << ")" << endl;
    } else {
        cerr << "[Pando] [DEBUG] Memory limits: off" << endl;
    }
    // Process-wide, and must be set before any entry is created
    TagTable::set_interning(intern_tags);
    ParDBOptions opts;
    opts.skip_group_filters = skip_group_filters;
    opts.max_filters_per_stage = max_filters_per_stage;
    opts.mem_policy = mem_policy;
    opts.write_batch_bytes = write_batch_bytes;
    opts.scan_block_keys = scan_block_keys;
    opts.continuous_processing = continuous_processing;
    ParDBThread db { bind_addr, sz, opts };

    if (seed_addr.size() != 0) {
        elga::ZMQAddress seed = get_zmq_addr(seed_addr);
        if (seed.serialize() != bind_addr.serialize()) {
            // Build a client and request to add the neighbor
            cerr << "[Pando] [INFO] Connecting to neighbor..." << endl;
            cerr << "[Pando] [DEBUG] Neighbor addr=" << seed.get_conn_str(bind_addr, REQUEST) << endl;
            ParDBClient c { bind_addr };
            c.add_neighbor(seed);
        }
    }

    // Setup the handler
    g_shutdown = db.get_shutdown_flag();
    set_handler();

    cerr << "[Pando] [INFO] Running" << endl;
    db.join();

    return 0;
}

int main(int argc, char **argv) {
    elga::ZMQChatterbox::Setup();

    int ret;

    try {
        ret = main_(argc, argv);
    } catch(...) {
        elga::ZMQChatterbox::Teardown();
        throw;
    }

    elga::ZMQChatterbox::Teardown();

    return ret;
}
