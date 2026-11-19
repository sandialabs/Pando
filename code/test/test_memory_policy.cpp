// The memory policy's building blocks (memory_policy.hpp): reading system
// memory, the per-pass budget, and the CLOSE_EARLY payload. How SeqDB and
// ParDB use them is tested in test_seq_db and test_filter_stats.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "address.hpp"
#include "memory_policy.hpp"

#include "test.hpp"

using namespace std;
using namespace pando;

static const uint64_t GiB = 1ull << 30;

TEST(parse_meminfo_basic) {
    istringstream in {
        "MemTotal:       16318228 kB\n"
        "MemFree:          722604 kB\n"
        "MemAvailable:    9876543 kB\n"
        "Buffers:           12345 kB\n"
        "HugePages_Total:       0\n"
    };
    MemInfo m = parse_meminfo(in);
    EQ(m.ok, true);
    EQ(m.total_bytes, 16318228ull * 1024);
    EQ(m.available_bytes, 9876543ull * 1024);

    TEST_PASS
}

TEST(parse_meminfo_incomplete) {
    // Kernels before 3.14 have no MemAvailable
    {
        istringstream in { "MemTotal:  1000 kB\nMemFree:  500 kB\n" };
        EQ(parse_meminfo(in).ok, false);
    }
    {
        istringstream in { "MemAvailable:  1000 kB\n" };
        EQ(parse_meminfo(in).ok, false);
    }
    {
        istringstream in { "" };
        EQ(parse_meminfo(in).ok, false);
    }
    // Garbage lines are skipped, not misread
    {
        istringstream in { "garbage\nMemTotal: x kB\n\nMemTotal: 10 kB\nMemAvailable: 4 kB\n" };
        MemInfo m = parse_meminfo(in);
        EQ(m.ok, true);
        EQ(m.total_bytes, 10*1024);
        EQ(m.available_bytes, 4*1024);
    }
    // A zero total is not usable
    {
        istringstream in { "MemTotal: 0 kB\nMemAvailable: 0 kB\n" };
        EQ(parse_meminfo(in).ok, false);
    }

    TEST_PASS
}

TEST(read_meminfo_files) {
    EQ(read_meminfo("/nonexistent/meminfo").ok, false);

    // Pando runs on Linux, where the real file is readable
    MemInfo m = read_meminfo();
    EQ(m.ok, true);
    EQ(m.total_bytes > 0, true);
    EQ(m.available_bytes <= m.total_bytes, true);

    TEST_PASS
}

TEST(policy_defaults) {
    // On by default: keep memory below 90%, a quarter of the headroom per
    // pass, agents per node worked out
    MemoryPolicy p;
    p.validate();
    EQ(p.enabled(), true);
    FEQ(p.limit_pct, 90);
    FEQ(p.budget_fraction, 0.25);
    EQ(p.pass_budget_bytes, 0);
    EQ(p.agents_per_node, 0);

    MemoryPolicy off = MemoryPolicy::disabled();
    off.validate();
    EQ(off.enabled(), false);

    // Either limit on its own enables it
    MemoryPolicy fixed = MemoryPolicy::disabled();
    fixed.pass_budget_bytes = 1;
    EQ(fixed.enabled(), true);
    MemoryPolicy limit = MemoryPolicy::disabled();
    limit.limit_pct = 50;
    EQ(limit.enabled(), true);

    TEST_PASS
}

TEST(policy_validate) {
    auto invalid = [](auto modify) {
        MemoryPolicy p;
        modify(p);
        try {
            p.validate();
        } catch (const invalid_argument&) {
            return true;
        }
        return false;
    };
    EQ(invalid([](MemoryPolicy& p) { p.budget_fraction = 0; }), true);
    EQ(invalid([](MemoryPolicy& p) { p.budget_fraction = -0.5; }), true);
    EQ(invalid([](MemoryPolicy& p) { p.budget_fraction = 1.01; }), true);
    EQ(invalid([](MemoryPolicy& p) { p.limit_pct = -0.1; }), true);
    EQ(invalid([](MemoryPolicy& p) { p.limit_pct = 100.1; }), true);
    EQ(invalid([](MemoryPolicy& p) { p.check_interval = -chrono::nanoseconds(1); }), true);
    // The ends of the ranges are allowed, and 0 agents per node means auto
    EQ(invalid([](MemoryPolicy& p) { p.agents_per_node = 0; }), false);
    EQ(invalid([](MemoryPolicy& p) { p.limit_pct = 0; }), false);
    EQ(invalid([](MemoryPolicy& p) { p.budget_fraction = 1; }), false);
    EQ(invalid([](MemoryPolicy& p) { p.limit_pct = 100; }), false);
    EQ(invalid([](MemoryPolicy& p) { p.check_interval = chrono::nanoseconds(0); }), false);

    TEST_PASS
}

TEST(pass_budget) {
    MemoryPolicy p;
    p.limit_pct = 75;
    p.budget_fraction = 0.5;
    MemInfo m;
    m.ok = true;
    m.total_bytes = 100*GiB;

    // reserve = 25% of 100 GiB; headroom = 55 - 25 = 30 GiB;
    // budget = 0.5 * 30 / 3 agents = 5 GiB
    m.available_bytes = 55*GiB;
    PassBudget b = compute_pass_budget(p, m, 3);
    EQ(b.reserve_bytes, 25*GiB);
    EQ(b.budget_bytes, 5*GiB);
    // 0 agents is treated as 1
    EQ(compute_pass_budget(p, m, 0).budget_bytes, 15*GiB);

    // Exactly at the limit, and over it: no budget, same reserve
    m.available_bytes = 25*GiB;
    b = compute_pass_budget(p, m, 3);
    EQ(b.reserve_bytes, 25*GiB);
    EQ(b.budget_bytes, 0);
    m.available_bytes = 3*GiB;
    b = compute_pass_budget(p, m, 3);
    EQ(b.reserve_bytes, 25*GiB);
    EQ(b.budget_bytes, 0);

    // A 100% limit reserves nothing
    p.limit_pct = 100;
    p.budget_fraction = 1;
    m.available_bytes = 7*GiB;
    b = compute_pass_budget(p, m, 1);
    EQ(b.reserve_bytes, 0);
    EQ(b.budget_bytes, 7*GiB);

    // The defaults: reserve 10%, a quarter of the rest
    MemoryPolicy d;
    m.total_bytes = 200*GiB;
    m.available_bytes = 100*GiB;
    b = compute_pass_budget(d, m, 4);
    // reserve = 20 GiB, headroom = 80 GiB, budget = 0.25 * 80 / 4 = 5 GiB
    // (allow for the 10% not being exact in floating point)
    EQ(b.reserve_bytes / (1u << 20), 20*1024);
    EQ(b.budget_bytes / (1u << 20), 5*1024);

    TEST_PASS
}

TEST(slurm_tasks_per_node) {
    // --ntasks-per-node, as in slurm/pardb.sbatch
    EQ(parse_slurm_tasks_per_node("16", nullptr), 16);
    EQ(parse_slurm_tasks_per_node("16", "8(x2)"), 16);
    // Otherwise the job's layout, if every node has the same count
    EQ(parse_slurm_tasks_per_node(nullptr, "16"), 16);
    EQ(parse_slurm_tasks_per_node(nullptr, "16(x4)"), 16);
    EQ(parse_slurm_tasks_per_node("garbage", "4(x2)"), 4);
    // Mixed counts do not say which is ours
    EQ(parse_slurm_tasks_per_node(nullptr, "2(x3),1"), 0);
    EQ(parse_slurm_tasks_per_node(nullptr, "4,4"), 0);
    // Unset or malformed: unknown
    EQ(parse_slurm_tasks_per_node(nullptr, nullptr), 0);
    EQ(parse_slurm_tasks_per_node("", ""), 0);
    EQ(parse_slurm_tasks_per_node("0", nullptr), 0);
    EQ(parse_slurm_tasks_per_node("-3", nullptr), 0);
    EQ(parse_slurm_tasks_per_node("16x", nullptr), 0);
    EQ(parse_slurm_tasks_per_node(nullptr, "16(x)"), 0);
    EQ(parse_slurm_tasks_per_node(nullptr, "16(4)"), 0);
    EQ(parse_slurm_tasks_per_node(nullptr, "16(x4"), 0);
    EQ(parse_slurm_tasks_per_node(nullptr, "(x4)"), 0);
    EQ(parse_slurm_tasks_per_node("99999999999999999999999", nullptr), 0);

    TEST_PASS
}

TEST(agents_sharing_ip) {
    elga::ZMQAddress self { "10.0.0.1", 0 };
    vector<addr_t> agents {
        self.serialize(),
        elga::ZMQAddress { "10.0.0.1", 3 }.serialize(),
        elga::ZMQAddress { "10.0.0.1", 6 }.serialize(),
        elga::ZMQAddress { "10.0.0.2", 0 }.serialize(),
        elga::ZMQAddress { "10.0.0.2", 3 }.serialize(),
        // Same local number, other host
        elga::ZMQAddress { "10.0.1.1", 0 }.serialize(),
    };
    EQ(count_agents_sharing_ip(agents, self.serialize()), 3);
    EQ(count_agents_sharing_ip(agents, elga::ZMQAddress { "10.0.0.2", 9 }.serialize()), 2);
    EQ(count_agents_sharing_ip(agents, elga::ZMQAddress { "10.9.9.9", 0 }.serialize()), 0);
    EQ(count_agents_sharing_ip(vector<addr_t>{}, self.serialize()), 0);

    TEST_PASS
}

TEST(close_early_round_trip) {
    CloseEarlyRequest r;
    r.agent = 0x123456789abcull;
    r.pass = 42;
    r.reason = STOP_MEM_PRESSURE;
    StatsWriter w { CLOSE_EARLY };
    r.serialize(w);

    // The message type comes first
    EQ((uint8_t)w.data()[0], CLOSE_EARLY);

    CloseEarlyRequest q;
    EQ(q.deserialize(w.data()+1, w.data()+w.size()), true);
    EQ(q.agent, r.agent);
    EQ(q.pass, 42);
    EQ(q.reason, STOP_MEM_PRESSURE);

    // Truncations and trailing garbage are rejected
    for (size_t len = 1; len < w.size(); ++len) {
        CloseEarlyRequest t;
        EQ(t.deserialize(w.data()+1, w.data()+len), false);
    }
    string extra { w.data(), w.size() };
    extra.push_back('x');
    CloseEarlyRequest t;
    EQ(t.deserialize(extra.data()+1, extra.data()+extra.size()), false);

    TEST_PASS
}

TEST(stop_reason_names) {
    // pando_top shows these
    EQ(string(stop_reason_name(STOP_NONE)), "none");
    EQ(string(stop_reason_name(STOP_MAX_FILTERS)), "run limit");
    EQ(string(stop_reason_name(STOP_MEM_BUDGET)), "memory budget");
    EQ(string(stop_reason_name(STOP_MEM_PRESSURE)), "memory pressure");
    EQ(string(stop_reason_name(STOP_REMOTE)), "another agent");
    EQ(string(stop_reason_name(200)), "?");

    TEST_PASS
}

// --- Memory limits from cgroups (find_cgroup_memory, apply_cgroup_limit) ---

static const uint64_t MiB_ = 1ull << 20;

// A scratch directory holding a fake cgroup file system
struct FakeCgroupRoot {
    std::string dir;
    FakeCgroupRoot() {
        char tmpl[] = "/tmp/pando_cgroup_test_XXXXXX";
        if (!mkdtemp(tmpl)) throw std::runtime_error("mkdtemp failed");
        dir = tmpl;
    }
    ~FakeCgroupRoot() { std::filesystem::remove_all(dir); }
    void write(const std::string& rel, const std::string& text) {
        std::filesystem::path f = dir + "/" + rel;
        std::filesystem::create_directories(f.parent_path());
        std::ofstream(f) << text;
    }
};

// cgroup v2: the limit is on an ancestor (the job), not the process's own
// cgroup (the task); usage leaves out file page cache
TEST(cgroup_v2_limit_on_ancestor) {
    FakeCgroupRoot r;
    r.write("memory.max", "max\n");
    r.write("slurm/job_1/memory.max", std::to_string(1024*MiB_) + "\n");
    r.write("slurm/job_1/memory.current", std::to_string(512*MiB_) + "\n");
    r.write("slurm/job_1/memory.stat", "anon 1\ninactive_file " + std::to_string(128*MiB_) + "\nactive_file " + std::to_string(64*MiB_) + "\n");
    r.write("slurm/job_1/step_0/memory.max", "max\n");
    r.write("slurm/job_1/step_0/memory.current", "1\n");
    CgroupMemory cg = find_cgroup_memory("0::/slurm/job_1/step_0\n", r.dir);
    EQ(cg.ok, true);
    EQ(cg.limit_bytes, 1024*MiB_);
    EQ(cg.used_bytes, 320*MiB_);

    MemInfo node; node.ok = true; node.total_bytes = 400*1024*MiB_; node.available_bytes = 300*1024*MiB_;
    MemInfo m = apply_cgroup_limit(node, cg);
    EQ(m.ok, true);
    EQ(m.total_bytes, 1024*MiB_);
    EQ(m.available_bytes, 704*MiB_);

    TEST_PASS
}

// The tightest of several limits on the path wins
TEST(cgroup_v2_tightest_limit) {
    FakeCgroupRoot r;
    r.write("a/memory.max", std::to_string(2048*MiB_));
    r.write("a/memory.current", std::to_string(100*MiB_));
    r.write("a/b/memory.max", std::to_string(512*MiB_));
    r.write("a/b/memory.current", std::to_string(200*MiB_));
    CgroupMemory cg = find_cgroup_memory("0::/a/b\n", r.dir);
    EQ(cg.ok, true);
    EQ(cg.limit_bytes, 512*MiB_);
    EQ(cg.used_bytes, 200*MiB_);

    TEST_PASS
}

// cgroup v1: the memory controller's own hierarchy, and its "no limit" value
TEST(cgroup_v1_limit) {
    FakeCgroupRoot r;
    r.write("memory/memory.limit_in_bytes", "9223372036854771712\n");
    r.write("memory/slurm/job_2/memory.limit_in_bytes", std::to_string(2048*MiB_));
    r.write("memory/slurm/job_2/memory.usage_in_bytes", std::to_string(1024*MiB_));
    r.write("memory/slurm/job_2/memory.stat", "cache 9\nactive_file 1\ninactive_file 2\n"
            "total_active_file " + std::to_string(8*MiB_) + "\ntotal_inactive_file " + std::to_string(24*MiB_) + "\n");
    CgroupMemory cg = find_cgroup_memory("12:pids:/x\n4:cpuset,memory:/slurm/job_2\n", r.dir);
    EQ(cg.ok, true);
    EQ(cg.limit_bytes, 2048*MiB_);
    // The hierarchical total_* counts win over this cgroup's own
    EQ(cg.used_bytes, 992*MiB_);

    TEST_PASS
}

// File page cache counts as available on both lists. Measured on a real
// cgroup: reading a 1 GiB file a second time moved its clean pages from
// inactive_file to active_file, and memory.current - inactive_file went up
// by 1 GiB while nothing more was in use. Shared memory (tmpfs, e.g. a
// /scratch in RAM) is on neither list and stays counted as used.
TEST(cgroup_active_file_pages_are_available) {
    FakeCgroupRoot r;
    r.write("job/memory.max", std::to_string(4096*MiB_));
    r.write("job/memory.current", std::to_string(2640*MiB_));
    r.write("job/memory.stat", "anon " + std::to_string(290*MiB_) + "\nfile " + std::to_string(2282*MiB_) +
            "\nactive_file " + std::to_string(1109*MiB_) + "\ninactive_file " + std::to_string(149*MiB_) +
            "\nshmem " + std::to_string(1024*MiB_) + "\n");
    CgroupMemory cg = find_cgroup_memory("0::/job\n", r.dir);
    EQ(cg.ok, true);
    // 2640 - 1109 - 149: anon, shmem and kernel memory, not the page cache
    EQ(cg.used_bytes, 1382*MiB_);

    // No memory.stat: all of memory.current counts as used
    r.write("job2/memory.max", std::to_string(4096*MiB_));
    r.write("job2/memory.current", std::to_string(100*MiB_));
    EQ(find_cgroup_memory("0::/job2\n", r.dir).used_bytes, 100*MiB_);

    TEST_PASS
}

// No limit anywhere, no cgroup, or unreadable files: the node's memory is used
TEST(cgroup_without_limit) {
    FakeCgroupRoot r;
    r.write("memory.max", "max\n");
    r.write("u/memory.max", "max\n");
    EQ(find_cgroup_memory("0::/u\n", r.dir).ok, false);
    EQ(find_cgroup_memory("", r.dir).ok, false);
    EQ(find_cgroup_memory("0::/does/not/exist\n", r.dir).ok, false);
    EQ(find_cgroup_memory("0::/u\n", r.dir + "/missing").ok, false);

    MemInfo node; node.ok = true; node.total_bytes = 100; node.available_bytes = 60;
    MemInfo m = apply_cgroup_limit(node, CgroupMemory{});
    EQ(m.total_bytes, 100);
    EQ(m.available_bytes, 60);

    TEST_PASS
}

// Usage at or over the limit leaves nothing available; a cgroup larger than
// the node changes nothing; the node's free memory still bounds a roomy cgroup
TEST(cgroup_limit_edges) {
    MemInfo node; node.ok = true; node.total_bytes = 1000; node.available_bytes = 300;
    CgroupMemory full; full.ok = true; full.limit_bytes = 500; full.used_bytes = 600;
    MemInfo m = apply_cgroup_limit(node, full);
    EQ(m.total_bytes, 500);
    EQ(m.available_bytes, 0);

    CgroupMemory big; big.ok = true; big.limit_bytes = 5000; big.used_bytes = 10;
    m = apply_cgroup_limit(node, big);
    EQ(m.total_bytes, 1000);
    EQ(m.available_bytes, 300);

    MemInfo none;    // node memory unreadable: the cgroup alone
    m = apply_cgroup_limit(none, full);
    EQ(m.ok, true);
    EQ(m.total_bytes, 500);

    TEST_PASS
}

// The whole read, from files
TEST(effective_meminfo_from_files) {
    FakeCgroupRoot r;
    r.write("proc_meminfo", "MemTotal:  4194304 kB\nMemFree: 1 kB\nMemAvailable:  2097152 kB\n");
    r.write("proc_cgroup", "0::/job\n");
    r.write("cg/job/memory.max", std::to_string(1024*MiB_));
    r.write("cg/job/memory.current", std::to_string(256*MiB_));
    MemInfo m = read_effective_meminfo(r.dir + "/proc_meminfo", r.dir + "/proc_cgroup", r.dir + "/cg");
    EQ(m.ok, true);
    EQ(m.total_bytes, 1024*MiB_);
    EQ(m.available_bytes, 768*MiB_);

    // Without a cgroup file, the node's memory
    m = read_effective_meminfo(r.dir + "/proc_meminfo", r.dir + "/no_such_file", r.dir + "/cg");
    EQ(m.total_bytes, 4096*MiB_);
    EQ(m.available_bytes, 2048*MiB_);

    TEST_PASS
}

TESTS_BEGIN
    RUN_TEST(parse_meminfo_basic)
    RUN_TEST(parse_meminfo_incomplete)
    RUN_TEST(read_meminfo_files)
    RUN_TEST(policy_defaults)
    RUN_TEST(policy_validate)
    RUN_TEST(pass_budget)
    RUN_TEST(slurm_tasks_per_node)
    RUN_TEST(agents_sharing_ip)
    RUN_TEST(close_early_round_trip)
    RUN_TEST(stop_reason_names)
    RUN_TEST(cgroup_v2_limit_on_ancestor)
    RUN_TEST(cgroup_v2_tightest_limit)
    RUN_TEST(cgroup_v1_limit)
    RUN_TEST(cgroup_active_file_pages_are_available)
    RUN_TEST(cgroup_without_limit)
    RUN_TEST(cgroup_limit_edges)
    RUN_TEST(effective_meminfo_from_files)
TESTS_END
