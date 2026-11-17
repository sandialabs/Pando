#include <iostream>
#include <string>

#include "process_stats.hpp"

#include "test.hpp"

using namespace std;
using namespace pando;

static FilterCounters make_counters(uint64_t base) {
    FilterCounters c;
    c.checked = base + 1;
    c.ran = base + 2;
    c.run_ns = base + 3;
    c.entries_out = base + 4;
    c.tags_added = base + 5;
    c.tags_removed = base + 6;
    c.val_updates = base + 7;
    return c;
}

TEST(pass_lifecycle) {
    ProcessStats s;
    EQ(s.pass, 0);
    EQ(s.pass_kind, PASS_NONE);

    // Ending a pass that never started records nothing
    s.end_pass();
    EQ(s.history.size(), 0);

    s.begin_pass(PASS_SINGLE);
    EQ(s.pass, 1);
    auto& f = s.filter("A", PASS_SINGLE);
    f.pass.ran = 3;
    f.pass.checked = 10;
    s.keys_total = 10;

    s.begin_pass(PASS_SINGLE);
    EQ(s.pass, 2);
    EQ(s.history.size(), 1);
    EQ(s.history[0].pass, 1);
    EQ(s.history[0].keys_total, 10);
    EQ(s.history[0].filters["A"].ran, 3);
    // The finished pass is folded into the totals and the pass counters reset
    EQ(s.filters["A"].pass.ran, 0);
    EQ(s.filters["A"].prior.ran, 3);
    EQ(s.keys_total, 0);

    s.filters["A"].pass.ran = 2;
    EQ(s.filters["A"].total().ran, 5);

    // end_pass is idempotent
    s.end_pass();
    s.end_pass();
    EQ(s.history.size(), 2);
    EQ(s.filters["A"].prior.ran, 5);

    TEST_PASS
}

TEST(history_capped) {
    ProcessStats s;
    for (size_t i = 0; i < ProcessStats::max_history + 10; ++i)
        s.begin_pass(PASS_SINGLE);
    s.end_pass();
    EQ(s.history.size(), ProcessStats::max_history);
    EQ(s.history.back().pass, ProcessStats::max_history + 10);

    TEST_PASS
}

TEST(progress_round_trip) {
    ProcessStats s;
    s.begin_pass(PASS_GROUP);
    s.keys_total = 100;
    s.keys_scanned = 42;
    s.stopped_early = true;
    s.group_iter = 7;
    s.filter("BTC_block_to_tx", PASS_SINGLE).pass = make_counters(10);
    s.filter("BTC_block_to_tx", PASS_SINGLE).prior = make_counters(100);
    s.filter("group_f", PASS_GROUP).pass = make_counters(20);

    ProgressSnapshot p;
    p.agent = 0x123456789ull;
    p.time_ms = 1700000000000ull;
    p.db_size = 999;
    p.state = STAGE_ALG_PROCESS;
    p.at_barrier = true;
    p.set_stats(s);

    StatsWriter w { PROGRESS };
    p.serialize(w);

    // The message type comes first
    EQ((uint8_t)w.data()[0], PROGRESS);

    ProgressSnapshot q;
    EQ(q.deserialize(w.data()+1, w.data()+w.size()), true);
    EQ(q.agent, p.agent);
    EQ(q.time_ms, p.time_ms);
    EQ(q.db_size, 999);
    EQ(q.state, STAGE_ALG_PROCESS);
    EQ(q.at_barrier, true);
    EQ(q.pass, 1);
    EQ(q.pass_kind, PASS_GROUP);
    EQ(q.pass_start_ms, s.pass_start_ms);
    EQ(q.keys_total, 100);
    EQ(q.keys_scanned, 42);
    EQ(q.stopped_early, true);
    EQ(q.group_iter, 7);
    EQ(q.filters.size(), 2);
    EQ(q.filters["BTC_block_to_tx"].kind, PASS_SINGLE);
    NOPRINT_EQ(q.filters["BTC_block_to_tx"].pass, make_counters(10));
    NOPRINT_EQ(q.filters["BTC_block_to_tx"].total, make_counters(10) + make_counters(100));
    EQ(q.filters["group_f"].kind, PASS_GROUP);
    NOPRINT_EQ(q.filters["group_f"].pass, make_counters(20));

    TEST_PASS
}

TEST(progress_truncated) {
    ProgressSnapshot p;
    p.filters["some_filter"] = FilterSnapshot{};
    StatsWriter w { PROGRESS };
    p.serialize(w);

    // Every truncation must be rejected rather than read past the end
    for (size_t len = 1; len < w.size(); ++len) {
        ProgressSnapshot q;
        EQ(q.deserialize(w.data()+1, w.data()+len), false);
    }
    // Trailing garbage is rejected too
    string extra { w.data(), w.size() };
    extra.push_back('x');
    ProgressSnapshot q;
    EQ(q.deserialize(extra.data()+1, extra.data()+extra.size()), false);

    TEST_PASS
}

TEST(stage_event_round_trip) {
    StageEvent e;
    e.agent = 77;
    e.time_ms = 123456;
    e.kind = EVENT_PASS_END;
    e.from_state = STAGE_CLOSING;
    e.to_state = STAGE_BEGIN;
    e.summary.pass = 4;
    e.summary.kind = PASS_SINGLE;
    e.summary.start_ms = 1000;
    e.summary.duration_ms = 2500;
    e.summary.keys_total = 50;
    e.summary.stopped_early = true;
    e.summary.filters["f1"] = make_counters(1);
    e.summary.filters["f2"] = make_counters(2);

    StatsWriter w { STAGE_EVENT };
    e.serialize(w);

    StageEvent d;
    EQ(d.deserialize(w.data()+1, w.data()+w.size()), true);
    EQ(d.agent, 77);
    EQ(d.time_ms, 123456);
    EQ(d.kind, EVENT_PASS_END);
    EQ(d.from_state, STAGE_CLOSING);
    EQ(d.to_state, STAGE_BEGIN);
    EQ(d.summary.pass, 4);
    EQ(d.summary.kind, PASS_SINGLE);
    EQ(d.summary.start_ms, 1000);
    EQ(d.summary.duration_ms, 2500);
    EQ(d.summary.keys_total, 50);
    EQ(d.summary.stopped_early, true);
    EQ(d.summary.filters.size(), 2);
    NOPRINT_EQ(d.summary.filters["f1"], make_counters(1));
    NOPRINT_EQ(d.summary.filters["f2"], make_counters(2));

    // A state change carries no filters
    StageEvent sc;
    sc.kind = EVENT_STATE_CHANGE;
    StatsWriter w2 { STAGE_EVENT };
    sc.serialize(w2);
    StageEvent d2;
    EQ(d2.deserialize(w2.data()+1, w2.data()+w2.size()), true);
    EQ(d2.summary.filters.size(), 0);

    // Truncations are rejected
    for (size_t len = 1; len < w.size(); ++len) {
        StageEvent t;
        EQ(t.deserialize(w.data()+1, w.data()+len), false);
    }

    TEST_PASS
}

TEST(state_names) {
    // GET_STATE replies use these names; keep them stable
    EQ(string(state_name(PRELOAD)), "PRELOAD");
    EQ(string(state_name(STAGE_BEGIN)), "STAGE_BEGIN");
    EQ(string(state_name(STAGE_PROCESSED)), "STAGE_PROCESSED");
    EQ(string(state_name(STAGE_CLOSING)), "STAGE_CLOSING");
    EQ(string(state_name(STAGE_ALG_PROCESS)), "STAGE_ALG_PROCESS");

    TEST_PASS
}

TESTS_BEGIN
    RUN_TEST(pass_lifecycle)
    RUN_TEST(history_capped)
    RUN_TEST(progress_round_trip)
    RUN_TEST(progress_truncated)
    RUN_TEST(stage_event_round_trip)
    RUN_TEST(state_names)
TESTS_END
