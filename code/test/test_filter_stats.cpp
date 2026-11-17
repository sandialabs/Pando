// Per-filter processing statistics (process_stats.hpp) as collected by the
// DBs and published by ParDB for pando_top.

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "par_db.hpp"

#include "test.hpp"

using namespace std;
using namespace pando;

// ---------------------------------------------------------------------------
// In-memory filters with exactly known behavior

static bool has_tag(const DBAccess* access, const string& tag) {
    for (auto tags = access->tags; (*tags)[0] != '\0'; ++tags)
        if (tag == *tags) return true;
    return false;
}

// SINGLE_ENTRY: runs on entries tagged "in" (once), adding one tag and one
// new entry
static bool g_single_should_run(const DBAccess* access) {
    return has_tag(access, "in") && !has_tag(access, "done");
}
static void g_single_run(void* raw) {
    DBAccess* access = (DBAccess*)raw;
    const char* const new_tags[] = {"out", ""};
    access->make_new_entry.run(&access->make_new_entry, new_tags, "v", INITIAL_KEY);
    access->add_tag.run(&access->add_tag, "done");
}
static FilterInterface g_single {
    filter_name: "TEST_single",
    filter_type: SINGLE_ENTRY,
    should_run: &g_single_should_run,
    init: nullptr,
    destroy: nullptr,
    run: &g_single_run
};

TEST(single_entry_counts) {
    SeqDB db;
    {DBEntry<> e; e.add_tag("in"); e.set_key({1,1,1}); db.add_entry(move(e));}
    {DBEntry<> e; e.add_tag("in"); e.set_key({1,2,1}); db.add_entry(move(e));}
    {DBEntry<> e; e.add_tag("x"); e.set_key({1,3,1}); db.add_entry(move(e));}

    db.install_filter(make_shared<Filter>(&g_single));

    // A zero interval reports after every entry
    size_t calls = 0;
    uint64_t last_scanned = 0;
    bool monotonic = true;
    db.set_progress_callback([&]() {
        ++calls;
        if (db.stats().keys_scanned != last_scanned + 1) monotonic = false;
        last_scanned = db.stats().keys_scanned;
        if (last_scanned == db.stats().keys_total) last_scanned = 0;
    }, chrono::seconds(0));

    db.process();

    auto& s = db.stats();
    // Pass 1 runs the filter; pass 2 finds nothing to do and ends processing
    EQ(s.pass, 2);
    EQ(s.pass_kind, PASS_NONE);
    EQ(s.history.size(), 2);

    auto& p1 = s.history[0];
    EQ(p1.pass, 1);
    EQ(p1.kind, PASS_SINGLE);
    EQ(p1.keys_total, 3);
    EQ(p1.stopped_early, false);
    EQ(p1.filters.at("TEST_single").checked, 3);
    EQ(p1.filters.at("TEST_single").ran, 2);
    EQ(p1.filters.at("TEST_single").entries_out, 2);
    EQ(p1.filters.at("TEST_single").tags_added, 2);
    EQ(p1.filters.at("TEST_single").tags_removed, 0);
    EQ(p1.filters.at("TEST_single").val_updates, 0);
    EQ(p1.filters.at("TEST_single").run_ns > 0, true);

    auto& p2 = s.history[1];
    EQ(db.size(), 5);
    EQ(p2.keys_total, 5);
    EQ(p2.filters.at("TEST_single").checked, 5);
    EQ(p2.filters.at("TEST_single").ran, 0);

    // Totals fold in every pass, and nothing is left in the pass counters
    auto& f = s.filters.at("TEST_single");
    EQ(f.total().ran, 2);
    EQ(f.total().checked, 8);
    EQ(f.pass.ran, 0);

    EQ(calls, 3+5);
    EQ(monotonic, true);

    TEST_PASS
}

// Outputs made outside of a filter run are not attributed to any filter
TEST(outputs_outside_filter) {
    SeqDB db;
    db.install_filter(make_shared<Filter>(&g_single));
    db.note_entry_out();
    db.note_tag_added();
    {DBEntry<> e; e.add_tag("x"); e.set_key({1,3,1}); db.add_entry(move(e));}
    db.process();
    EQ(db.stats().filters.at("TEST_single").total().entries_out, 0);
    EQ(db.stats().filters.at("TEST_single").total().tags_added, 0);

    TEST_PASS
}

// GROUP_ENTRIES: always runs, goes inactive at once, and creates one entry
static void* g_forward_init(DBAccess* access) { return (void*)access; }
static void g_forward_destroy([[maybe_unused]] void* state) { }
static bool g_group_should_run([[maybe_unused]] const DBAccess* access) { return true; }
static void g_group_run(void* raw) {
    DBAccess* access = (DBAccess*)raw;
    access->group->state = INACTIVE;
    const char* const new_tags[] = {"FOO", ""};
    access->make_new_entry.run(&access->make_new_entry, new_tags, "BAR", INITIAL_KEY);
}
static FilterInterface g_group {
    filter_name: "TEST_group",
    filter_type: GROUP_ENTRIES,
    should_run: &g_group_should_run,
    init: &g_forward_init,
    destroy: &g_forward_destroy,
    run: &g_group_run
};

TEST(group_counts) {
    AlgDB db;
    // Vertices (graph 0): 0, 5 and 7
    {DBEntry<> e; e.add_tag("e"); e.set_key({0,0,5}); db.add_entry(move(e));}
    {DBEntry<> e; e.add_tag("e"); e.set_key({0,0,7}); db.add_entry(move(e));}
    {DBEntry<> e; e.add_tag("e"); e.set_key({0,5,7}); db.add_entry(move(e));}

    db.install_filter(make_shared<Filter>(&g_group));
    db.process();

    auto& s = db.stats();
    // One (empty) single-entry pass, then the group pass
    EQ(s.history.size(), 2);
    EQ(s.history[0].kind, PASS_SINGLE);
    EQ(s.history[0].filters.count("TEST_group"), 0);

    auto& g = s.history[1];
    EQ(g.kind, PASS_GROUP);
    EQ(g.filters.at("TEST_group").checked, 3);
    EQ(g.filters.at("TEST_group").ran, 3);
    EQ(g.filters.at("TEST_group").entries_out, 3);
    EQ(s.filters.at("TEST_group").kind, PASS_GROUP);
    EQ(s.group_iter, 1);

    TEST_PASS
}

// ---------------------------------------------------------------------------
// The real BTC filters

// The filters key entries on the first 15 hex digits of the txid, so vary
// those
static string hex_txid(size_t i) {
    stringstream ss;
    ss << hex << (0xabc000 + i);
    string s = ss.str();
    return s + string(64 - s.size(), '0');
}

static DBEntry<> make_block(dbkey_t key, size_t num_txs) {
    DBEntry<> e;
    e.set_key(key);
    e.add_tag("BTC").add_tag("block");
    string v = "{\"time\":1234,\"tx\":[";
    for (size_t i = 0; i < num_txs; ++i) {
        if (i > 0) v += ",";
        v += "{\"txid\":\"" + hex_txid(i) + "\"}";
    }
    v += "]}";
    e.add_to_value(v);
    return e;
}

static const size_t g_num_txs = 5;

TEST(btc_filter_counts) {
    SeqDB db;
    db.add_filter_dir(build_dir+"/filters");
    db.install_filter("BTC_block_to_tx");
    db.install_filter("BTC_block_to_txtime");
    db.add_entry(make_block({1,2,4}, g_num_txs));

    db.process();

    // The block, plus one tx and one txtime entry per transaction
    EQ(db.size(), 1 + 2*g_num_txs);

    auto& s = db.stats();
    EQ(s.history.size(), 2);
    for (string name : {"BTC_block_to_tx", "BTC_block_to_txtime"}) {
        auto& p1 = s.history[0].filters.at(name);
        EQ(p1.checked, 1);
        EQ(p1.ran, 1);
        EQ(p1.entries_out, g_num_txs);
        EQ(p1.tags_added, 1);

        auto& p2 = s.history[1].filters.at(name);
        EQ(p2.checked, 1 + 2*g_num_txs);
        EQ(p2.ran, 0);
    }

    TEST_PASS
}

// ---------------------------------------------------------------------------
// ParDB publishes PROGRESS and STAGE_EVENT messages

/** @brief Collects what pando_top would see */
class StatsListener : public PandoParticipant {
    public:
        map<addr_t, ProgressSnapshot> progress;
        vector<StageEvent> events;
        size_t bad_msgs = 0;

        StatsListener() : PandoParticipant(elga::ZMQAddress{}, false) {
            sub(PROGRESS);
            sub(STAGE_EVENT);
        }
        void process_msg(msg_type_t type, [[maybe_unused]] zmq_socket_t sock, const char *data, const char *end) {
            if (type == PROGRESS) {
                ProgressSnapshot p;
                if (p.deserialize(data, end)) progress[p.agent] = p;
                else ++bad_msgs;
            } else if (type == STAGE_EVENT) {
                StageEvent e;
                if (e.deserialize(data, end)) events.push_back(e);
                else ++bad_msgs;
            } else
                recv_unknown_msg();
        }
        void pump(chrono::milliseconds dur) {
            auto until = chrono::steady_clock::now() + dur;
            while (chrono::steady_clock::now() < until) recv_msg(10);
        }
};

TEST(par_db_publishes_stats) {
    elga::ZMQAddress db1_addr { "127.0.0.1", 180 };
    ParDBThread db1 { db1_addr };
    elga::ZMQAddress db2_addr { "127.0.0.1", 183 };
    ParDBThread db2 { db2_addr };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };
    c1.add_neighbor(db2_addr);
    this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.num_neighbors(), 2);

    // Join the mesh as pando_top does, and wait for heartbeats to tell us
    // about both agents and for the subscriptions to settle
    StatsListener l;
    l.add_neighbor(db1_addr.serialize(), false);
    l.request_mesh(db1_addr.serialize());
    l.pump(chrono::milliseconds(2500));
    EQ(l.agents().size(), 2);
    EQ(l.progress.size(), 2);
    for (auto& [agent, p] : l.progress) {
        EQ(p.state, PRELOAD);
        EQ(p.pass, 0);
    }

    c1.add_filter_dir(build_dir+"/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("BTC_block_to_tx");
    c1.install_filter("BTC_block_to_txtime");
    DBEntry<> block = make_block({1,2,4}, g_num_txs);
    c1.add_entry(block);
    this_thread::sleep_for(chrono::milliseconds(50));

    c1.process();

    // Keep listening until the whole mesh is done
    auto deadline = chrono::steady_clock::now() + chrono::seconds(60);
    do {
        l.pump(chrono::milliseconds(200));
    } while ((c1.processing() || c2.processing()) && chrono::steady_clock::now() < deadline);
    // Collect the final heartbeats
    l.pump(chrono::milliseconds(1500));

    EQ(l.bad_msgs, 0);
    EQ(c1.db_size() + c2.db_size(), 1 + 2*g_num_txs);

    // Every agent reported its passes; summed over the mesh, each filter ran
    // once on the block in pass 1, and not again
    map<uint64_t, map<string, FilterCounters>> by_pass;
    map<addr_t, vector<uint8_t>> states;
    for (auto& e : l.events) {
        if (e.kind == EVENT_PASS_END) {
            for (auto& [name, c] : e.summary.filters)
                by_pass[e.summary.pass][name] += c;
        } else {
            states[e.agent].push_back(e.to_state);
        }
    }
    EQ(by_pass.count(1), 1);
    for (string name : {"BTC_block_to_tx", "BTC_block_to_txtime"}) {
        EQ(by_pass[1][name].ran, 1);
        EQ(by_pass[1][name].entries_out, g_num_txs);
        EQ(by_pass[1][name].tags_added, 1);
        uint64_t total_ran = 0;
        for (auto& [pass, filters] : by_pass) total_ran += filters[name].ran;
        EQ(total_ran, 1);
    }

    // Both agents went through the stages of a pass and ended up idle
    EQ(states.size(), 2);
    for (auto& [agent, seq] : states) {
        EQ(seq.empty(), false);
        EQ(seq.front(), STAGE_BEGIN);
        EQ(seq.back(), PRELOAD);
        EQ((size_t)count(seq.begin(), seq.end(), STAGE_PROCESSED) >= 1, true);
        EQ((size_t)count(seq.begin(), seq.end(), STAGE_CLOSING) >= 1, true);
    }

    // The last progress reports agree with the events
    uint64_t total_ran = 0;
    for (auto& [agent, p] : l.progress) {
        EQ(p.state, PRELOAD);
        EQ(p.at_barrier, false);
        EQ(p.pass >= 2, true);
        if (p.filters.count("BTC_block_to_tx"))
            total_ran += p.filters["BTC_block_to_tx"].total.ran;
    }
    EQ(total_ran, 1);

    TEST_PASS
}

// Installed filters are reported before any processing, so pando_top can
// show what is installed; each pass only summarizes filters of its kind
TEST(installed_filters_reported_before_processing) {
    AlgDB db;
    db.install_filter(make_shared<Filter>(&g_single));
    db.install_filter(make_shared<Filter>(&g_group));

    auto& s = db.stats();
    EQ(s.pass, 0);
    EQ(s.filters.size(), 2);
    EQ(s.filters.at("TEST_single").kind, PASS_SINGLE);
    EQ(s.filters.at("TEST_group").kind, PASS_GROUP);
    NOPRINT_EQ(s.filters.at("TEST_single").total(), FilterCounters{});

    // ... and carried in the PROGRESS payload
    ProgressSnapshot p;
    p.set_stats(s);
    StatsWriter w { PROGRESS };
    p.serialize(w);
    ProgressSnapshot q;
    EQ(q.deserialize(w.data() + sizeof(msg_type_t), w.data() + w.size()), true);
    EQ(q.filters.size(), 2);
    EQ(q.filters.at("TEST_group").kind, PASS_GROUP);

    {DBEntry<> e; e.add_tag("e"); e.set_key({0,0,5}); db.add_entry(move(e));}
    db.process();
    EQ(s.history.size(), 2);
    EQ(s.history[0].kind, PASS_SINGLE);
    EQ(s.history[0].filters.count("TEST_single"), 1);
    EQ(s.history[0].filters.count("TEST_group"), 0);
    EQ(s.history[1].kind, PASS_GROUP);
    EQ(s.history[1].filters.count("TEST_single"), 0);
    EQ(s.history[1].filters.count("TEST_group"), 1);

    TEST_PASS
}

TESTS_BEGIN
    elga::ZMQChatterbox::Setup();

    RUN_TEST(single_entry_counts)
    RUN_TEST(outputs_outside_filter)
    RUN_TEST(group_counts)
    RUN_TEST(installed_filters_reported_before_processing)
    RUN_TEST(btc_filter_counts)
    RUN_TEST(par_db_publishes_stats)

    elga::ZMQChatterbox::Teardown();
TESTS_END
