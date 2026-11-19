#include "test.hpp"

#include <sstream>
#include <vector>
#include <string>
#include <set>
#include <thread>
#include <unordered_set>

#include "dbentry.hpp"

using namespace std;
using namespace pando;

TEST(add_tags) {
    DBEntry<> db;

    db.add_tag("first");
    db.add_tag("second");
    db.add_tag("third");

    EQ(db.tag_size(), 3);

    EQ(db.has_tag("no"), false);
    EQ(db.has_tag("second"), true);
    EQ(db.has_tag("third"), true);
    EQ(db.has_tag("no"), false);

    db.add_tag("fourth").add_tag("fifth");
    EQ(db.tag_size(), 5);
    EQ(db.has_tag("fourth"), true);
    EQ(db.has_tag("fifth"), true);

    db.add_tag("6", "7", "8");
    EQ(db.has_tag("6"), true);
    EQ(db.has_tag("7"), true);
    EQ(db.has_tag("8"), true);

    TEST_PASS
}

TEST(remove_tag) {
    DBEntry<> e;

    e.add_tag("first", "second", "third");
    EQ(e.tag_size(), 3);

    e.remove_tag("first", "fourth");

    EQ(e.tag_size(), 2);
    EQ(e.has_tag("first"), false);
    EQ(e.has_tag("second"), true);
    EQ(e.has_tag("third"), true);

    //Verify that the c tags got updated
    bool second_found = false;
    bool third_found = false;
    const char* const* tags = e.c_tags();
    while ((*tags)[0] != '\0') {
        if (string(*tags) == "first")
            TEST_FAIL
        else if (string(*tags) == "second") {
            if (second_found)
                TEST_FAIL
            second_found = true;
        } else if (string(*tags) == "third") {
            if (third_found)
                TEST_FAIL
            third_found = true;
        } else {
            TEST_FAIL
        }
        ++tags;
    }

    EQ(second_found, true);
    EQ(third_found, true);


    TEST_PASS
}

TEST(check_value) {
    DBEntry<> db;

    EQ(db.value(), "");
    db.add_to_value("X");
    EQ(db.value(), "X\n");
    const string& v {db.value()};
    EQ(v, "X\n");
    db.add_to_value("another string");
    EQ(db.value(), "X\nanother string\n");
    EQ(v, "X\nanother string\n");

    string& edit_v {db.value()};
    EQ(edit_v, "X\nanother string\n");
    edit_v.assign("--");
    EQ(db.value(), "--");
    EQ(v, "--");

    db.add_to_value("a").add_to_value("b");
    EQ(v, "--a\nb\n");

    TEST_PASS
}

TEST(clear) {
    DBEntry<> db;

    EQ(db.tag_size(), 0);
    EQ(db.value(), "");

    db.add_tag("t1");
    EQ(db.tag_size(), 1);
    EQ(db.value(), "");

    db.add_to_value("Y");
    EQ(db.tag_size(), 1);
    EQ(db.value(), "Y\n");

    auto& r = db.clear();
    EQ(r.tag_size(), 0);
    EQ(r.value(), "");
    EQ(db.tag_size(), 0);
    EQ(db.value(), "");

    TEST_PASS
}

TEST(c_tags) {
    DBEntry<> db;

    db.add_tag("first");
    db.add_tag("second");
    db.add_tag("third");

    EQ(db.tag_size(), 3);

    auto t = db.c_tags();
    auto p = t;

    vector<string> tags;

    while (*p[0] != '\0') {
        tags.emplace_back(*p++);
    }
    EQ(tags.size(), 3);

    bool t1 = false;
    bool t2 = false;
    bool t3 = false;

    for (size_t idx = 0; idx < 3; ++idx) {
        string tag { t[idx] };
        if (tag == "first") {
            if (t1) TEST_FAIL
            t1 = true;
        } else if (tag == "second") {
            if (t2) TEST_FAIL
            t2 = true;
        } else if (tag == "third") {
            if (t3) TEST_FAIL
            t3 = true;
        } else
            TEST_FAIL
    }
    EQ(t1 && t2 && t3, true);

    TEST_PASS
}

TEST(tags) {
    DBEntry<> db;

    db.add_tag("first");
    db.add_tag("second");
    db.add_tag("third");

    EQ(db.tag_size(), 3);

    auto tags = db.tags();

    EQ(tags.size(), 3);

    bool t1 = false;
    bool t2 = false;
    bool t3 = false;

    for (auto &tag : tags) {
        if (tag == "first") {
            if (t1) TEST_FAIL
            t1 = true;
        } else if (tag == "second") {
            if (t2) TEST_FAIL
            t2 = true;
        } else if (tag == "third") {
            if (t3) TEST_FAIL
            t3 = true;
        } else
            TEST_FAIL
    }
    EQ(t1 && t2 && t3, true);

    TEST_PASS
}

TEST(clear_f) {
    DBEntry<> db;

    NOPRINT_NOTEQ(db.c_tags(), nullptr);

    db.add_tag("Tag");

    auto t = db.c_tags();
    NOPRINT_NOTEQ(t, nullptr);
    EQ(t[0][0], 'T');
    EQ(t[1][0], '\0');

    db.clear();

    t = db.c_tags();
    NOPRINT_NOTEQ(t, nullptr);
    EQ(t[0][0], '\0');

    db.add_tag("Tag");

    t = db.c_tags();
    NOPRINT_NOTEQ(t, nullptr);
    EQ(t[0][0], 'T');
    EQ(t[1][0], '\0');

    EQ(db.valid_c_tags(), true);
    db.clear(true);
    EQ(db.valid_c_tags(), false);
    try {
        db.c_tags();
        TEST_FAIL
    } catch(...) {}

    TEST_PASS
}

TEST(init_key) {
    DBEntry<> e;
    e.clear(true);
    auto k = e.get_key();
    EQ(k, INITIAL_KEY);
    EQ(k.a, INIT_CH_IN);
    EQ(k.b, -1);
    EQ(k.c, -1);

    TEST_PASS
}

TEST(key) {
    DBEntry<> e;
    e.set_key({3, 5, 10});
    auto k_copy = e.get_key();
    EQ(k_copy.a, 3);
    EQ(k_copy.b, 5);
    EQ(k_copy.c, 10);
    auto& k = e.get_key();
    EQ(k.a, 3);
    EQ(k.b, 5);
    EQ(k.c, 10);

    dbkey_t k2 = {17, 15, 20};
    e.set_key(k2);
    EQ(k.a, 17);
    EQ(k.b, 15);
    EQ(k.c, 20);
    EQ(k_copy.a, 3);
    EQ(k_copy.b, 5);
    EQ(k_copy.c, 10);

    TEST_PASS
}

TEST(copy_constructor) {
    DBEntry<> db1;

    db1.add_tag("t1");
    db1.add_tag("t2");
    string& db1_val = db1.value();
    db1_val.assign("some content");
    db1.set_key({1,0,0});

    EQ(db1.value(), "some content");
    EQ(db1.tag_size(), 2);
    EQ(db1.random_key(), false);
    EQ(db1.get_key().a, 1);
    EQ(db1.get_key().b, 0);
    EQ(db1.get_key().c, 0);

    DBEntry<> db2 { db1 };

    EQ(db2.value(), "some content");
    EQ(db2.tag_size(), 2);
    EQ(db2.get_key(), INITIAL_KEY);
    NOTEQ(db2.get_key().a, 1);
    NOTEQ(db2.get_key().b, 0);
    NOTEQ(db2.get_key().c, 0);
    auto c2 = db2.get_key().c;
    db1_val.assign("new");
    db1.add_tag("t3");
    db1.set_key({15,20, 22});
    EQ(db1.tag_size(), 3);
    EQ(db2.tag_size(), 2);
    EQ(db1.value(), "new");
    EQ(db2.value(), "some content");
    EQ(db1.get_key().a, 15);
    EQ(db1.get_key().b, 20);
    EQ(db1.get_key().c, 22);
    NOTEQ(db2.get_key().a, 1);
    NOTEQ(db2.get_key().b, 0);
    EQ(db2.get_key().c, c2);

    TEST_PASS
}

TEST(copy_assignment) {
    DBEntry<> db1;

    db1.add_tag("t1");
    db1.add_tag("t2");
    string& db1_val = db1.value();
    db1_val.assign("some content");
    db1.set_key({1,0,0});

    DBEntry<> db2;
    db2.add_tag("x");

    EQ(db1.value(), "some content");
    EQ(db1.tag_size(), 2);
    EQ(db2.get_key().a, INIT_CH_IN);
    EQ(db2.get_key().b, -1);
    EQ(db2.get_key().c, -1);
    auto c2 = db2.get_key().c;

    db2 = db1;
    EQ(db2.get_key(), INITIAL_KEY);
    c2 = db2.get_key().c;
    EQ(db2.value(), "some content");
    EQ(db2.tag_size(), 2);
    db1_val.assign("new");
    db1.add_tag("t3");
    db1.set_key({50,40,30});
    EQ(db1.tag_size(), 3);
    EQ(db2.tag_size(), 2);
    EQ(db1.value(), "new");
    EQ(db2.value(), "some content");
    NOTEQ(db2.get_key().a, 1);
    NOTEQ(db2.get_key().b, 0);
    EQ(db2.get_key().c, c2);
    EQ(db1.get_key().a, 50);
    EQ(db1.get_key().b, 40);
    EQ(db1.get_key().c, 30);

    TEST_PASS
}

TEST(move_constructor) {
    DBEntry<> db1;

    db1.add_tag("t1");
    db1.add_tag("t2");
    string& db1_val = db1.value();
    db1_val.assign("some content");
    db1.set_key({1,0,0});

    EQ(db1.value(), "some content");
    EQ(db1.tag_size(), 2);
    EQ(db1.get_key().a, 1);
    EQ(db1.get_key().b, 0);
    EQ(db1.get_key().c, 0);

    DBEntry<> db2 {move(db1)};
    EQ(db2.value(), "some content");
    EQ(db2.tag_size(), 2);
    EQ(db1_val, "")
    try {
        db1.tag_size();
        TEST_FAIL
    } catch(...) {}
    EQ(db1.get_key().a, INIT_CH_IN);
    EQ(db1.get_key().b, -1);
    EQ(db1.get_key().c, -1);
    EQ(db2.get_key().a, 1);
    EQ(db2.get_key().b, 0);
    EQ(db2.get_key().c, 0);
    EQ(db2.tag_size(), 2);
    EQ(db2.value(), "some content");

    TEST_PASS
}

TEST(move_assignment) {
    DBEntry<> db1;

    db1.add_tag("t1");
    db1.add_tag("t2");
    string& db1_val = db1.value();
    db1_val.assign("some content");
    db1.set_key({1,0,0});

    EQ(db1.value(), "some content");
    EQ(db1.tag_size(), 2);
    EQ(db1.get_key().a, 1);
    EQ(db1.get_key().b, 0);
    EQ(db1.get_key().c, 0);

    DBEntry<> db2;
    db2.add_tag("x");
    NOPRINT_NOTEQ(db2.c_tags(), nullptr);

    db2 = move(db1);

    EQ(db2.value(), "some content");
    EQ(db2.tag_size(), 2);
    EQ(db1_val, "")
    try {
        db1.tag_size();
        TEST_FAIL
    } catch(...) {}
    EQ(db1.get_key().a, INIT_CH_IN);
    EQ(db1.get_key().b, -1);
    EQ(db1.get_key().c, -1);
    EQ(db2.get_key().a, 1);
    EQ(db2.get_key().b, 0);
    EQ(db2.get_key().c, 0);
    EQ(db2.tag_size(), 2);
    EQ(db2.value(), "some content");

    TEST_PASS
}

TEST(access) {
    DBEntry<> db;

    auto access = db.access();

    EQ(access->key, db.get_key());

    delete access;

    TEST_PASS
}

TEST(access_tagval) {
    DBEntry<> db;

    db.add_tag("k1", "k2");
    EQ(db.tag_size(), 2);

    auto access = db.access();

    EQ(access->tags, db.c_tags());
    EQ(access->value, db.value().c_str());

    delete access;

    TEST_PASS
}

TEST(build_from_c) {
    const char* tags[] = {"t1", "t2", ""};
    const char* value = "test val";

    DBEntry<> e {tags, value};

    EQ(e.tag_size(), 2);
    EQ(e.value(), "test val");
    EQ(e.has_tag("t1"), true);
    EQ(e.has_tag("t2"), true);

    TEST_PASS
}

TEST(empty_init) {
    DBEntry<> e;

    NOPRINT_NOTEQ(e.c_tags(), nullptr);

    TEST_PASS
}

TEST(fail_on_invalid) {
    DBEntry<> start_entry;
    start_entry.add_tag("t1");
    start_entry.add_tag("t2");
    start_entry.add_to_value("content");
    EQ(start_entry.tag_size(), 2);
    EQ(start_entry.valid_c_tags(), true);

    auto moved_entry = move(start_entry);

    try { start_entry.add_tag("a"); TEST_FAIL; } catch(...) {}
    try { start_entry.add_to_value("a"); TEST_FAIL; } catch(...) {}
    try { start_entry.tag_size(); TEST_FAIL; } catch(...) {}
    try { start_entry.has_tag("asdf"); TEST_FAIL; } catch(...) {}
    try { start_entry.has_tag("t1"); TEST_FAIL; } catch(...) {}
    try { start_entry.c_tags(); TEST_FAIL; } catch(...) {}
    EQ(start_entry.valid_c_tags(), false);
    try { start_entry.c_tags(); TEST_FAIL; } catch(...) {}
    try {
        const DBEntry<>& c_entry = start_entry;
        [[maybe_unused]] const string& v = c_entry.value();
        TEST_FAIL;
    } catch(...) {}
    try { [[maybe_unused]] string& v = start_entry.value(); TEST_FAIL; } catch(...) {}
    try { start_entry.access(); TEST_FAIL; } catch(...) {}

    TEST_PASS
}

TEST(const_value) {
    DBEntry<> e;
    e.add_to_value("some value");
    string &v1 = e.value();
    EQ(v1, "some value\n");
    v1.assign("next");
    EQ(e.value(), "next");
    v1 = "further";
    EQ(e.value(), "further");

    const DBEntry<>& c = e;

    EQ(e.value(), "further");
    EQ(c.value(), "further");
    e.add_to_value("x");
    EQ(e.value(), "furtherx\n");
    EQ(c.value(), "furtherx\n");

    TEST_PASS
}

TEST(print_entry) {
    DBEntry<> e;
    e.add_tag("test1").add_tag("cout_test");
    EQ(e.has_tag("test1"), true);
    EQ(e.has_tag("cout_test"), true);
    e.value() = "test_value";

    stringstream ss;
    ss << e;
    string s = ss.str();

    if (s != "KEY:\n(random)\nTAGS:\ncout_test\ntest1\nVALUE:\ntest_value" &&
        s != "KEY:\n(random)\nTAGS:\ntest1\ncout_test\nVALUE:\ntest_value")
        TEST_FAIL

    TEST_PASS
}

TEST(print_entry_key) {
    DBEntry<> e;
    e.add_tag("test1").add_tag("cout_test");
    EQ(e.has_tag("test1"), true);
    EQ(e.has_tag("cout_test"), true);
    e.value() = "test_value";
    e.set_key({3,4,5});

    stringstream ss;
    ss << e;
    string s = ss.str();

    if (s != "KEY:\n3:4:5\nTAGS:\ncout_test\ntest1\nVALUE:\ntest_value" &&
        s != "KEY:\n3:4:5\nTAGS:\ntest1\ncout_test\nVALUE:\ntest_value")
        TEST_FAIL

    TEST_PASS
}

TEST(build_with_key) {
    const char* const tags[] = {"test1", "test2", ""};
    const char* value = "test_value";
    dbkey_t key {1,2,3};
    DBEntry<> e {tags, value, key};

    EQ(e.get_key().a, 1);
    EQ(e.get_key().b, 2);
    EQ(e.get_key().c, 3);

    TEST_PASS
}

TEST(not_equal) {
    dbkey_t e1_key = {10,10,10};
    dbkey_t e4_key = {11,10,10};
    DBEntry<> e1; e1.value() = "foo\nbar"; e1.add_tag("test1", "test2"); e1.set_key(e1_key);
    DBEntry<> e2; e2.value() = "foo\nbar"; e2.add_tag("test1", "test2"); e2.set_key(e1_key);
    DBEntry<> e3; e3.value() = "foo\nbar"; e3.add_tag("test1", "test3"); e3.set_key(e1_key);
    DBEntry<> e4; e4.value() = "foo\nbar"; e4.add_tag("test1", "test2"); e4.set_key(e4_key);
    DBEntry<> e5; e5.value() = "oo\nbar"; e5.add_tag("test1", "test2"); e5.set_key(e1_key);

    //Everything the same
    EQ(false, e1 != e2);

    //Different tags
    EQ(true, e1 != e3);

    //Different key
    EQ(true, e1 != e4);

    //Different value
    EQ(true, e1 != e5);

    TEST_PASS
}

TEST(serialize) {
    dbkey_t e1_key = {10,10,10};
    DBEntry<> e1; e1.value() = "foo\nbar"; e1.add_tag("test1", "test2"); e1.set_key(e1_key);

    char* e1s = new char[e1.serialize_size()];
    char* e1s_ptr = e1s;
    EQ((unsigned long)e1s_ptr, (unsigned long)e1s);

    e1.serialize(e1s_ptr);
    NOTEQ((unsigned long)e1s_ptr, (unsigned long)e1s);

    const char* e1s_des = e1s;
    DBEntry<> e2 {e1s_des};
    EQ((unsigned long)e1s_ptr, (unsigned long)e1s_des);

    EQ(false, e1 != e2);

    // Test that e1s moved up
    delete [] e1s;
    
    TEST_PASS
}

// --- Tag interning (tag_table.hpp) ---

// Restores interning to its default when a test ends
struct InterningScope {
    explicit InterningScope(bool on) { TagTable::set_interning(on); }
    ~InterningScope() { TagTable::set_interning(true); }
};

static set<string> c_tag_set(const DBEntry<>& e) {
    set<string> res;
    const char* const* t = e.c_tags();
    for (; *t[0] != '\0'; ++t) res.insert(*t);
    return res;
}

// Each distinct tag is stored once and keeps its id
TEST(tag_table_interns_each_tag_once) {
    TagTable& table = TagTable::global();
    size_t before = table.size();
    uint32_t a = table.intern("tag_table_test_a");
    uint32_t b = table.intern("tag_table_test_b");
    EQ(table.intern("tag_table_test_a"), a);
    EQ(string(table.intern(string("tag_table_test_") + "b") == b ? "same" : "different"), "same");
    EQ(a != b, true);
    EQ(table.size(), before + 2);
    EQ(table.str(a), "tag_table_test_a");
    uint32_t id = 0;
    EQ(table.find("tag_table_test_b", id), true);
    EQ(id, b);
    EQ(table.find("tag_table_test_never_added", id), false);
    EQ(table.size(), before + 2);

    TEST_PASS
}

// Every tag operation gives the same results with interning on and off,
// including past the 4 tags an interned entry keeps inline
TEST(tags_same_with_and_without_interning) {
    for (bool on : {true, false}) {
        InterningScope scope {on};
        DBEntry<> e;
        EQ(e.tag_store().interned(), on);
        vector<string> names {"t0", "a longer tag than fifteen characters", "t2", "t3", "t4", "t5", "t6"};
        for (auto & n : names) e.add_tag(n);
        e.add_tag("t2");    // already there
        EQ(e.tag_size(), names.size());
        for (auto & n : names) EQ(e.has_tag(n), true);
        EQ(e.has_tag("missing"), false);
        EQ(c_tag_set(e) == set<string>(names.begin(), names.end()), true);
        EQ(e.tags() == unordered_set<string>(names.begin(), names.end()), true);

        // Shrink back below the inline size, then grow again
        for (size_t i = 2; i < names.size(); ++i) e.remove_tag(names[i]);
        e.remove_tag("missing");
        EQ(e.tag_size(), 2);
        EQ(c_tag_set(e) == set<string>({"t0", "a longer tag than fifteen characters"}), true);
        e.add_tag("t9");
        EQ(e.tag_size(), 3);
        EQ(e.has_tag("t3"), false);

        // Copies and moves keep the tags (and the mode)
        e.value() = "val";
        e.set_key({5, 6, 7});
        DBEntry<> copy {e};
        EQ(copy.tags() == e.tags(), true);
        EQ(copy.tag_store().interned(), on);
        DBEntry<> moved {move(copy)};
        EQ(moved.tags() == e.tags(), true);
        bool threw = false;
        try { copy.has_tag("t0"); } catch (runtime_error&) { threw = true; }
        EQ(threw, true);    // a moved-from entry is invalid, as before

        // Serialization round trip
        vector<char> buf(e.serialize_size());
        char* w = buf.data();
        e.serialize(w);
        EQ((size_t)(w - buf.data()), buf.size());
        const char* r = buf.data();
        DBEntry<> back {r};
        EQ(back == e, true);
        EQ(back.value(), "val");

        e.clear();
        EQ(e.tag_size(), 0);
        EQ(e.c_tags()[0][0], '\0');
    }

    TEST_PASS
}

// Entries made with interning on and off compare, copy and serialize into
// each other correctly
TEST(interned_and_plain_entries_mix) {
    DBEntry<> interned;
    {
        InterningScope scope {true};
        DBEntry<> e; e.add_tag("x").add_tag("y").add_tag("zzzzzzzzzzzzzzzzzzzz");
        interned = e;
    }
    DBEntry<> plain;
    {
        InterningScope scope {false};
        DBEntry<> e; e.add_tag("y").add_tag("zzzzzzzzzzzzzzzzzzzz").add_tag("x");
        plain = move(e);
    }
    EQ(interned.tag_store().interned(), true);
    EQ(plain.tag_store().interned(), false);
    EQ(interned == plain, true);
    EQ(plain == interned, true);
    plain.add_tag("w");
    EQ(interned == plain, false);

    DBEntry<> copied;
    copied.copy_tags_from(plain);
    EQ(copied.tags() == plain.tags(), true);

    // Serialized by one mode, read by the other
    {
        InterningScope scope {false};
        vector<char> buf(interned.serialize_size());
        char* w = buf.data();
        interned.serialize(w);
        const char* r = buf.data();
        DBEntry<> back {r};
        EQ(back.tag_store().interned(), false);
        EQ(back.tags() == interned.tags(), true);
    }

    TEST_PASS
}

// The strings an entry's C tags point to stay valid while the table grows
TEST(c_tags_survive_table_growth) {
    DBEntry<> e;
    e.add_tag("survivor_one").add_tag("survivor_two");
    const char* const* t = e.c_tags();
    vector<const char*> ptrs;
    for (; *t[0] != '\0'; ++t) ptrs.push_back(*t);
    for (size_t i = 0; i < 100000; ++i)
        TagTable::global().intern("growth_" + to_string(i));
    set<string> seen;
    for (auto p : ptrs) seen.insert(p);
    EQ(seen == set<string>({"survivor_one", "survivor_two"}), true);
    EQ(e.c_tags() == t - ptrs.size(), true);    // unchanged entry, same array

    TEST_PASS
}

// Interning from several threads at once gives each tag exactly one id
TEST(tag_table_threads) {
    const size_t threads = 8, tags = 2000;
    vector<vector<uint32_t>> ids(threads, vector<uint32_t>(tags));
    vector<thread> ts;
    for (size_t t = 0; t < threads; ++t)
        ts.emplace_back([&, t]() {
            for (size_t i = 0; i < tags; ++i) {
                size_t k = (i * 7 + t * 13) % tags;    // each thread in its own order
                ids[t][k] = TagTable::global().intern("thread_tag_" + to_string(k));
            }
        });
    for (auto & th : ts) th.join();
    set<uint32_t> distinct;
    for (size_t k = 0; k < tags; ++k) {
        for (size_t t = 1; t < threads; ++t) EQ(ids[t][k], ids[0][k]);
        EQ(TagTable::global().str(ids[0][k]), "thread_tag_" + to_string(k));
        distinct.insert(ids[0][k]);
    }
    EQ(distinct.size(), tags);

    TEST_PASS
}

TESTS_BEGIN
    RUN_TEST(add_tags)
    RUN_TEST(remove_tag)
    RUN_TEST(check_value)
    RUN_TEST(clear)
    RUN_TEST(c_tags)
    RUN_TEST(tags)
    RUN_TEST(clear_f)
    RUN_TEST(init_key)
    RUN_TEST(key)
    RUN_TEST(copy_constructor)
    RUN_TEST(copy_assignment)
    RUN_TEST(move_constructor)
    RUN_TEST(move_assignment)
    RUN_TEST(access)
    RUN_TEST(access_tagval)
    RUN_TEST(build_from_c)
    RUN_TEST(empty_init)
    RUN_TEST(fail_on_invalid)
    RUN_TEST(const_value)
    RUN_TEST(print_entry)
    RUN_TEST(print_entry_key)
    RUN_TEST(build_with_key)
    RUN_TEST(not_equal)
    RUN_TEST(serialize)
    RUN_TEST(tag_table_interns_each_tag_once)
    RUN_TEST(tags_same_with_and_without_interning)
    RUN_TEST(interned_and_plain_entries_mix)
    RUN_TEST(c_tags_survive_table_growth)
    RUN_TEST(tag_table_threads)
TESTS_END
