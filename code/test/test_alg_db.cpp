#include "test.hpp"
#include "alg_db.hpp"

using namespace std;
using namespace pando;

TEST(msg_data) {
    msg_t m;
    MData d;
    d.from_string("test");

    m[15][15].push_back(d);

    EQ(m[15][15].size(), 1);
    EQ(m[15][15][0].size(), 4);

    MData d2 { 10 };
    m[10][2].push_back(move(d2));
    auto& d2r = m[10][2][0];
    EQ(d2r.size(), 4);
    char* d2d = d2r.get();

    EQ(d2d[0], 10);
    EQ(d2d[1], 0x0);
    EQ(d2d[2], 0x0);
    EQ(d2d[3], 0x0);

    d2d[0] = 'O';
    d2d[1] = 'K';
    d2d[2] = 0;

    const char* d2valref = m[10][2][0].get();
    string d2val {d2valref};

    EQ(d2val, "OK");

    TEST_PASS
}

TEST(msg_size) {
    MData d { (uint64_t)5 };
    EQ(d.size(), 8);

    char* d_data = d.get();

    EQ(d_data[0], 5);
    for (size_t i = 1; i < 8; i++)
        EQ(d_data[i], 0x0);
    
    float x = 5.4f;
    d = {x};
    EQ(d.size(), 4);

    d = {8.};
    EQ(d.size(), 8);

    TEST_PASS
}

TEST(msg_copy) {
    string x = "msg test";
    MData d1;
    d1.from_string(x);
    MData d2 {3};
    EQ(d2.size(), 4);
    d2 = d1;

    EQ(d1.size(), 8);
    EQ(d2.size(), 8);
    EQ(string(d1.get()), string(d2.get()));

    // Ensure that the copy is shallow, that is they all point to the same data
    auto d1d = d1.get();
    d1d[0] = 'M';
    EQ(string(d1.get()), "Msg test");
    EQ(string(d2.get()), "Msg test");

    // Ensure a copy and a resize is deep, the first is not modified
    MData d3 {d2};
    EQ(string(d3.get()), "Msg test");
    d3.from_string("Msg test");
    d3.get()[0] = 'x';
    EQ(string(d3.get()), "xsg test");
    EQ(string(d1.get()), "Msg test");
    EQ(string(d2.get()), "Msg test");

    TEST_PASS
}

void* g_forward_init(DBAccess* access) { return (void*)access; }
void g_forward_destroy([[maybe_unused]] void* state) { return; }

static int g_TEST_filter_types_pass;
bool g_TEST_filter_types_sroe([[maybe_unused]] const DBAccess* access) { return true; }
void g_TEST_filter_types_run(void* access) {
    ((DBAccess*)access)->group->state = INACTIVE;
    ++g_TEST_filter_types_pass;
}

TEST(filter_types) {
    AlgDB db;

    // Populate the database
    {DBEntry<> e; e.clear().add_tag("e").set_key({0,0,5}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().add_tag("e").set_key({0,0,7}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().add_tag("e").set_key({0,5,7}); db.add_entry(move(e));}

    // This DB will result in 2 filter runs:
    //      0 {5,7}
    //      5 {7}
    //
    //      Note: no vertex 7 group access will be created and hence no alg
    //      process or alg filters will ever run on 'vertex 7' aka group access
    //      created for vertex 7!

    FilterInterface i {
        filter_name: "TEST",
        filter_type: GROUP_ENTRIES,
        should_run: &g_TEST_filter_types_sroe,
        init: &g_forward_init,
        destroy: &g_forward_destroy,
        run: &g_TEST_filter_types_run
    };
    g_TEST_filter_types_pass = 0;
    db.install_filter(make_shared<Filter>(&i));
    db.process();
    EQ(g_TEST_filter_types_pass, 3);        // 3 vertices: 0, 5, 7

    TEST_PASS
}

static int g_TEST_should_run_pass;
bool g_TEST_should_run_sroe(const DBAccess *acc) {
    // Only run on the graph key "3" and vertices between 5 and 10
    if (acc->key.a != 3) return false;
    if (acc->key.b < 5 || acc->key.b > 10) return false;
    return true;
}
void g_TEST_should_run_run(void* access) {
    ((DBAccess*)access)->group->state = INACTIVE;
    ++g_TEST_should_run_pass;
}
TEST(should_run) {
    // We want there to be 3 entries that pass and actually run, the rest fail
    AlgDB db;

    // Populate the database
    {DBEntry<> e; e.clear().set_key({3,6,12}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({3,6,2}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({3,6,1}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({3,6,99}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({3,0,7}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({3,5,7}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({3,5,99}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({3,10,0}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({3,11,0}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({3,12,0}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,7,12}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,7,2}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,7,1}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,7,99}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,0,9}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,5,7}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,5,99}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,10,0}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,11,0}); db.add_entry(move(e));}
    {DBEntry<> e; e.clear().set_key({2,12,0}); db.add_entry(move(e));}

    FilterInterface i {
        filter_name: "TEST",
        filter_type: GROUP_ENTRIES,
        should_run: &g_TEST_should_run_sroe,
        init: &g_forward_init,
        destroy: &g_forward_destroy,
        run: &g_TEST_should_run_run
    };
    g_TEST_should_run_pass = 0;
    db.install_filter(make_shared<Filter>(&i));
    db.process();
    EQ(g_TEST_should_run_pass, 4);

    TEST_PASS
}

static int g_TEST_should_run_tags_pass;
bool g_TEST_should_run_tags_sroe(const DBAccess *access) {
    auto group = access->group;
    auto outs = group->entry.out;

    bool found = false;
    for (auto &edge : outs) {
        auto tags = edge.acc->tags;

        while ((*tags)[0] != '\0') {
            if (string(*tags) == "test1")
                found = true;
            ++tags;
        }
    }

    return found;
}
void g_TEST_should_run_tags_run(void* access) {
    ((DBAccess*)access)->group->state = INACTIVE;
    ++g_TEST_should_run_tags_pass;
}
TEST(should_run_tags) {
    AlgDB db;

    // Populate the database
    chain_info_t i = 1;
    {DBEntry e; e.clear().add_tag("test1").set_key({i++,1,1}); e.value() = "1"; db.add_entry(move(e));}
    {DBEntry e; e.clear().add_tag("test2").set_key({i++,1,1}); e.value() = "2"; db.add_entry(move(e));}
    {DBEntry e; e.clear().add_tag("test3").set_key({i++,1,1}); e.value() = "3"; db.add_entry(move(e));}
    {DBEntry e; e.clear().add_tag("test1", "test2").set_key({i++,1,1}); e.value() = "4"; db.add_entry(move(e));}
    {DBEntry e; e.clear().add_tag("test1", "test3").set_key({i++,1,1}); e.value() = "5"; db.add_entry(move(e));}

    FilterInterface interface {
        filter_name: "TEST",
        filter_type: GROUP_ENTRIES,
        should_run: &g_TEST_should_run_tags_sroe,
        init: &g_forward_init,
        destroy: &g_forward_destroy,
        run: &g_TEST_should_run_tags_run
    };
    g_TEST_should_run_tags_pass = 0;
    db.install_filter(make_shared<Filter>(&interface));
    db.process();
    EQ(g_TEST_should_run_tags_pass, 3);

    TEST_PASS
}

bool g_TEST_create_entry_sroe([[maybe_unused]] const DBAccess* access) { return true; }
void g_TEST_create_entry_run([[maybe_unused]] void* access_raw) {
    DBAccess* access = (DBAccess*)access_raw;
    const char* const new_tags[] = {"FOO", ""};
    string new_value_str = "BAR";
    dbkey_t new_key {0,0,0};
    access->group->state = INACTIVE;
    access->make_new_entry.run(&access->make_new_entry, new_tags, new_value_str.c_str(), new_key);
}
TEST(create_entry) {
    //Test to ensure that entries can be created from a group filter, wasn't working previously
    AlgDB db;
    FilterInterface i {
        filter_name: "TEST",
        filter_type: GROUP_ENTRIES,
        should_run: &g_TEST_create_entry_sroe,
        init: &g_forward_init,
        destroy: &g_forward_destroy,
        run: &g_TEST_create_entry_run
    };

    {
        dbkey_t new_key = {1,0,0};
        DBEntry<> e; e.add_tag("test_tag"); e.set_key(new_key); e.value() = "test_val"; db.add_entry(move(e));
    }
    EQ(db.size(), 1);

    db.install_filter(make_shared<Filter>(&i));
    db.process();

    EQ(db.size(), 2);

    TEST_PASS
}

TEST(simple_group_test) {
    //Test to ensure that entries can be created from a group filter, wasn't working previously
    AlgDB db;

    db.add_filter_dir(build_dir+"/test/filters");

    {
        dbkey_t new_key = {1,1,5};
        DBEntry<> e; e.set_key(new_key); db.add_entry(move(e));
    }
    {
        dbkey_t new_key = {1,5,999};
        DBEntry<> e; e.set_key(new_key); db.add_entry(move(e));
    }
    EQ(db.size(), 2);

    db.install_filter("test_algdb_simple_group");
    db.process();

    EQ(db.size(), 3);

    TEST_PASS
}

// Group filters must be able to read each edge's entry (value and tags) while
// they run, not just during should_run/init. Each edge's DBAccess points into
// the entry rather than copying it, so the entries must outlive setup and stay
// alive through every compute iteration.
static const size_t G_EDGES_VALUE_LEN = 80;
static size_t g_TEST_edges_checked;
static vector<string> g_TEST_edges_errors;

static string g_TEST_edge_value(graph_t a, vtx_t b, vtx_t c) {
    string v = "edge " + to_string(a) + "/" + to_string(b) + "/" + to_string(c) + " ";
    v.resize(G_EDGES_VALUE_LEN, '.');
    return v;
}

bool g_TEST_edges_sroe(const DBAccess* acc) { return acc->key.a == 4; }

void g_TEST_edges_run(void* access) {
    DBAccess* acc = (DBAccess*)access;

    // Allocate strings the size of the edge values first, as a real filter
    // doing work would. If the entries had been freed, these reuse that
    // memory and the reads below see it.
    vector<string> churn;
    for (size_t i = 0; i < 4096; ++i)
        churn.emplace_back(G_EDGES_VALUE_LEN, 'Z');

    for (auto & edge : acc->group->entry.out) {
        string where = to_string(acc->group->vtx) + "->" + to_string(edge.n);

        string want = g_TEST_edge_value(acc->key.a, acc->group->vtx, edge.n);
        if (edge.acc->value == nullptr || string(edge.acc->value) != want) {
            // Skip the tags: if the value is wrong, the tag array may be too,
            // and walking it could crash instead of failing the test
            g_TEST_edges_errors.push_back("value of edge " + where);
            continue;
        }

        set<string> tags;
        size_t n = 0;
        for (auto t = edge.acc->tags; t != nullptr && (*t)[0] != '\0' && n < 8; ++t, ++n)
            tags.insert(*t);
        set<string> want_tags {"edge", "c=" + to_string(edge.n)};
        if (tags != want_tags)
            g_TEST_edges_errors.push_back("tags of edge " + where);

        ++g_TEST_edges_checked;
    }

    acc->group->state = INACTIVE;
}

TEST(group_edges_readable_in_run) {
    AlgDB db;

    const vtx_t NUM_VERTICES = 50, EDGES_PER_VERTEX = 20;
    for (vtx_t b = 1; b <= NUM_VERTICES; ++b) {
        for (vtx_t c = 1000; c < 1000 + EDGES_PER_VERTEX; ++c) {
            DBEntry<> e;
            e.clear().add_tag("edge").add_tag("c=" + to_string(c)).set_key({4, b, c});
            e.value() = g_TEST_edge_value(4, b, c);
            db.add_entry(move(e));
        }
    }

    FilterInterface i {
        filter_name: "TEST",
        filter_type: GROUP_ENTRIES,
        should_run: &g_TEST_edges_sroe,
        init: &g_forward_init,
        destroy: &g_forward_destroy,
        run: &g_TEST_edges_run
    };
    g_TEST_edges_checked = 0;
    g_TEST_edges_errors.clear();
    db.install_filter(make_shared<Filter>(&i));
    db.process();

    for (size_t k = 0; k < g_TEST_edges_errors.size() && k < 5; ++k)
        cerr << "  bad edge entry: " << g_TEST_edges_errors[k] << endl;
    EQ(g_TEST_edges_errors.size(), 0u);
    EQ(g_TEST_edges_checked, (size_t)(NUM_VERTICES * EDGES_PER_VERTEX));

    TEST_PASS
}

TESTS_BEGIN
    elga::ZMQChatterbox::Setup();
    RUN_TEST(msg_data)
    RUN_TEST(msg_size)
    RUN_TEST(msg_copy)
    RUN_TEST(filter_types)
    RUN_TEST(should_run)
    RUN_TEST(should_run_tags)
    RUN_TEST(create_entry)
    RUN_TEST(simple_group_test)
    RUN_TEST(group_edges_readable_in_run)
    elga::ZMQChatterbox::Teardown();
TESTS_END
