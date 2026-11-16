#include "test.hpp"
#include "seq_db.hpp"

using namespace std;
using namespace pando;

TEST(bad_file) {
    // @FIXME test a couple different bad files, make sure the parser either
    // fails gracefully or as expected
    TEST_PASS
}

TEST(open_close) {
    // Load the test data
    SeqDB db { data_dir+"/simple_bitcoin.txt" };

    // Ensure the number of entries is correct
    EQ(db.size(), 2);

    TEST_PASS
}

TEST(correct_tags) {
    SeqDB db { data_dir+"/simple_bitcoin.txt" };

    // We know simple bitcoin has two entries, each with:
    // bitcoin
    // block
    auto db_entries = db.entries();
    EQ(db_entries.size(), 2);

    for (auto & [key, entry] : db_entries) {
        EQ(entry.tag_size(), 2);
        EQ(entry.has_tag("BTC"), true);
        EQ(entry.has_tag("block"), true);
    }

    TEST_PASS
}

TEST(correct_c_tags) {
    SeqDB db { data_dir+"/simple_bitcoin.txt" };

    // We know simple bitcoin has two entries, each with:
    // bitcoin
    // block
    auto db_entries = db.entries();
    EQ(db_entries.size(), 2);

    bool bitcoin_found = false;
    bool block_found = false;
    for (const auto & [key, entry] : db_entries) {
        auto c_tags = entry.c_tags();
        size_t ctr = 0;
        const char* const* tag_ptr = c_tags;
        while ((*tag_ptr)[0] != '\0') {
            if (string(*tag_ptr) == "BTC")
                bitcoin_found = true;
            if (string(*tag_ptr) == "block")
                block_found = true;
            ++ctr;
            ++tag_ptr;
        }
        EQ(ctr, entry.tag_size());
        EQ(bitcoin_found, true);
        EQ(block_found, true);
    }

    TEST_PASS
}

TEST(correct_values) {
    SeqDB db { data_dir+"/simple_bitcoin.txt" };

    // We know simple bitcoin has two entries, each with:
    // bitcoin
    // block
    auto db_entries = db.entries();
    EQ(db_entries.size(), 2);

    // @FIXME leave this for now, but if it starts breaking on different
    // systems rework to check a simpler string from a new file or something
    // else
    hash<string> hasher;
    bool v1 = false;
    bool v2 = false;
    for (auto & [key, entry] : db_entries) {
        string str_val;
        str_val.assign(entry.value());
        if (!v1 && hasher(str_val) == 5619803994070295679)
            v1 = true;
        else if (!v2 && hasher(str_val) == 2458312292255981855)
            v2 = true;
        else
            TEST_FAIL
    }
    EQ(v1, true);
    EQ(v2, true);

    TEST_PASS
}

TEST(modify_entries) {
    SeqDB db { data_dir+"/simple_bitcoin.txt" };

    // We know simple bitcoin has two entries, each with:
    // bitcoin
    // block

    // Modify the first entry
    vector<DBEntry<>> db_entries;
    for (auto & [key, entry] : db.entries())
        db_entries.push_back(move(entry));

    EQ(db_entries.size(), 2);

    string first_val = db_entries[0].value();
    string second_val = db_entries[1].value();

    dbkey_t e0_key = db_entries[0].get_key();
    db.update_entry_val(e0_key, "TEST");

    string updated_val = db.entries()[e0_key].value();

    // Check the second entry
    NOTEQ(first_val, updated_val);
    EQ(second_val, db_entries[1].value());
    EQ(updated_val, "TEST");

    TEST_PASS
}

TEST(dup_filters_fail) {
    SeqDB db {};

    //just make sure it doesn't give an error and only installs once
    db.add_filter_dir(build_dir+"/filters");
    size_t num_filters = db.num_filters();
    db.add_filter_dir(build_dir+"/filters");
    EQ(num_filters, db.num_filters());
    TEST_PASS

}

TEST(bad_filter_dir) {
    // Test to make sure the DB doesn't crash if it receives a bad filter dir
    SeqDB db {};

    // Try to add a dir that doesn't exist
    db.add_filter_dir(build_dir+"/nonexistent");

    // Now, try to add one that does
    db.add_filter_dir(build_dir+"/filters");
    db.install_filter("BTC_block_to_tx");
    EQ(db.num_filters() > 0, true);

    TEST_PASS
}

TEST(install_filter_fail) {
    SeqDB db {};

    //make sure it doesn't install anything (should log warning though)
    db.install_filter("bad filter");
    EQ(db.num_filters(), 0);

    TEST_PASS
}

TEST(copy_entry) {
    SeqDB db { data_dir+"/simple_bitcoin.txt" };
    EQ(db.entries().size(), 2);

    auto entry1 = db.entries().begin()->second;
    auto entry2 = next(db.entries().begin())->second;

    DBEntry<> copy_entry1 = entry1;
    DBEntry<> copy_entry2;
    copy_entry2 = entry2;

    EQ(entry1.value(), copy_entry1.value());
    EQ(entry2.value(), copy_entry2.value());

    TEST_PASS
}

TEST(move_entry) {
    SeqDB db { data_dir+"/simple_bitcoin.txt" };
    EQ(db.entries().size(), 2);

    auto entry1 = db.entries().begin()->second;
    auto entry2 = next(db.entries().begin())->second;

    auto value1 = entry1.value();
    auto value2 = entry2.value();

    DBEntry<> move_entry1 = move(entry1);
    DBEntry<> move_entry2;
    move_entry2 = move(entry2);

    EQ(value1, move_entry1.value());
    EQ(value2, move_entry2.value());
    try {
        entry1.value();
        TEST_FAIL
    } catch(...) {}
    try {
        entry2.value();
        TEST_FAIL
    } catch(...) {}

    TEST_PASS
}

TEST(clear) {
    SeqDB db { data_dir+"/simple_bitcoin.txt" };
    EQ(db.entries().size(), 2);

    auto entry1 = db.entries().begin()->second;
    auto entry2 = next(db.entries().begin())->second;

    // Test without f flag
    entry1.clear();
    EQ(entry1.tag_size(), 0);
    EQ(entry1.value(), "");
    EQ((string)*entry1.c_tags(), "");

    // Test with f flag
    entry2.clear(true);
    try {
        entry2.c_tags();
        TEST_FAIL
    } catch(...) {}
    EQ(entry2.valid_c_tags(), false);

    TEST_PASS
}

TEST(stage_entries) {
    SeqDB db { data_dir+"/simple_bitcoin.txt" };
    EQ(db.size(), 2);

    DBEntry<> e1;
    e1.add_tag("e1");
    e1.add_tag("new");
    e1.add_to_value("sample");
    db.stage_add_entry(e1);

    EQ(db.size(), 2);

    DBEntry<> e2;
    e2.add_tag("e2");
    e2.add_tag("new");

    db.stage_add_entry(e2);
    EQ(db.size(), 2);

    db.stage_close();
    EQ(db.size(), 4);

    TEST_PASS
}

TEST(access) {
    DBEntry<> e;
    DBAccess *acc = e.access();

    DBEntry<> e1;
    e1.add_tag("test");
    DBEntry<> e2;
    e2.add_tag("test");

    SeqDB db;

    db.add_entry(e1);
    db.add_entry(e2);

    EQ(db.size(), 2);

    db.add_db_access(acc);

    EQ(db.size(), 2);
    for (auto& [key, entry] : db.entries())
        EQ(entry.has_tag("test"), true);

    {
        DBEntry<> n;
        n.add_tag("other");
        auto n_tags = n.c_tags();
        const char* n_value = n.value().c_str();

        acc->make_new_entry.run(&acc->make_new_entry, n_tags, n_value, INITIAL_KEY);
    }

    EQ(db.size(), 2);
    db.stage_close();
    EQ(db.size(), 3);

    int have_new = 0;
    int have_old = 0;
    for (auto& [key, entry] : db.entries()) {
        if (entry.has_tag("test")) ++have_old;
        if (entry.has_tag("other")) ++have_new;
        if (entry.has_tag("other")) EQ(entry.random_key(), true);
    }
    EQ(have_new, 1);
    EQ(have_old, 2);

    delete acc;

    TEST_PASS
}

TEST(access_tags) {
    SeqDB db;

    DBEntry<>* e_ptr = nullptr;

    dbkey_t key {1,1,1};
    { DBEntry<> e; e.add_tag("b"); db.add_entry(move(e)); }
    { DBEntry<> e; e.add_tag("b", "c"); db.add_entry(move(e)); }
    { DBEntry<> e; e.add_tag("b", "c", "d"); db.add_entry(move(e)); }
    { DBEntry<> e; e.add_tag("a", "c", "d"); db.add_entry(move(e)); }
    { DBEntry<> e; e.add_tag("a", "b").value() = "res"; e.set_key(key); db.add_entry(move(e)); }

    auto entries = db.entries();
    for (auto& [k, e] : entries) {
        if (!e.has_tag("a")) continue;
        if (!e.has_tag("b")) continue;
        e_ptr = &e;
        break;
    }

    DBEntry<> &e = *e_ptr;

    auto acc = e.access();

    EQ(e.has_tag("c"), false);
    db.add_db_access(acc);

    // Make sure {a,b} finds one item
    EQ(db.get_entry_by_tags({"a", "b"}).size(), 1);
    // Make sure {a,b,c} does not find anything
    EQ(db.get_entry_by_tags({"a", "b", "c"}).size(), 0);

    acc->add_tag.run(&acc->add_tag, "c");

    EQ(e.has_tag("c"), false);

    db.stage_close();

    e = db.entries()[key];
    bool abc_found = false;
    bool ab_found = false;
    size_t bc_count = 0;
    for (auto& [ret_key, entry] : db.entries()) {
        if (ret_key == key) {
            EQ(entry.has_tag("c"), true);
        }
        if (entry.has_tag("a") && entry.has_tag("b") && entry.has_tag("c")) {
            EQ(abc_found, false);
            abc_found = true;
        }
        if (entry.has_tag("a") && entry.has_tag("b")) {
            EQ(abc_found, true);
            EQ(ab_found, false);
            ab_found = true;
        }
        if (entry.has_tag("b") && entry.has_tag("c")) {
            bc_count++;
        }
    }

    EQ(ab_found, true);
    EQ(abc_found, true);
    EQ(bc_count, 3);

    acc->remove_tag.run(&acc->remove_tag, acc->key, "c");

    // Make sure it wasn't removed before stage_close
    bc_count = 0;
    for (auto& [ret_key, entry] : db.entries()) {
        if (entry.has_tag("b") && entry.has_tag("c")) {
            bc_count++;
        }
    }
    EQ(bc_count, 3);

    db.stage_close();

    bc_count = 0;
    for (auto& [ret_key, entry] : db.entries()) {
        if (entry.has_tag("b") && entry.has_tag("c")) {
            bc_count++;
        }
    }
    EQ(bc_count, 2);

    delete acc;

    TEST_PASS
}

TEST(get_entry_by_tags) {
    SeqDB db;

    DBEntry<> e1, e2, e3;
    dbkey_t e1_key = db.generate_random_key();
    dbkey_t e2_key = db.generate_random_key();
    dbkey_t e3_key = db.generate_random_key();
    e1.clear().add_tag("x1", "x2"); e1.set_key(e1_key); db.add_entry(move(e1));
    e2.clear().add_tag("y1", "x1"); e2.set_key(e2_key); db.add_entry(move(e2));
    e3.clear().add_tag("x2", "y2"); e3.set_key(e3_key); db.add_entry(move(e3));


    {
        const char* search_tags[] = {"x1", ""};
        auto key_set = db.get_entry_by_tags(search_tags);
        EQ(key_set.size(), 2);

        bool e1_found = false;
        bool e2_found = false;
        for (auto &key : key_set) {
            if (!e1_found && key == e1_key)
                e1_found = true;
            else if (!e2_found && key == e2_key)
                e2_found = true;
            else {
                cerr << "e1_found=" << e1_found << endl;
                cerr << "e2_found=" << e2_found << endl;
                TEST_FAIL
            }

        }

    }
    {
        const char* search_tags[] = {"x1", "x2", ""};
        auto key_set = db.get_entry_by_tags(search_tags);
        EQ(key_set.size(), 1);
        EQ(*(key_set.begin()), e1_key);
    }
    {
        const char* search_tags[] = {"y1", ""};
        auto key_set = db.get_entry_by_tags(search_tags);
        EQ(key_set.size(), 1);
        EQ(*(key_set.begin()), e2_key);
    }
    {
        const char* search_tags[] = {"x2", "y2", ""};
        auto key_set = db.get_entry_by_tags(search_tags);
        EQ(key_set.size(), 1);
        EQ(*(key_set.begin()), e3_key);
    }
    {
        const char* search_tags[] = {"x1", "foo", ""};
        auto key_set = db.get_entry_by_tags(search_tags);
        EQ(key_set.size(), 0);
    }

    TEST_PASS
}

TEST(get_entry_value_by_tags) {
    SeqDB db;
    DBEntry<> entry1;
    entry1.add_tag("test1").add_tag("test2").add_tag("test3").add_tag("test4");
    entry1.add_to_value("entry1value");
    DBEntry<> entry2;
    entry2.add_tag("test1").add_tag("test2").add_tag("test3").add_tag("test4");
    entry2.add_to_value("entry2value");
    db.add_entry(entry1);
    db.add_entry(entry2);
    EQ(db.size(), 2);

    const char* tags[] = {"test1", "test2", ""};
    char* cret;
    db.get_entry_value_by_tags(tags, &cret);
    NOPRINT_NOTEQ(cret, nullptr);
    string ret { cret };
    free(cret);

    bool success = false;
    if (ret == "entry1value\n" || ret == "entry2value\n") success = true;
    EQ(success, true);

    DBEntry<> entry3;
    entry3.add_tag("needle").add_tag("haystack").add_tag("test4").add_tag("test2");
    entry3.value().assign("X");
    db.add_entry(entry3);
    const char* s2[] = {"needle", "haystack", ""};
    db.get_entry_value_by_tags(s2, &cret);
    NOPRINT_NOTEQ(cret, nullptr);
    ret.assign(cret);
    free(cret);
    EQ(ret, "X");

    // Make a test that: has the /first tag/ include everything, and then other
    // tags include just one
    const char* s3[] = {"test2", "needle", ""};
    cret = nullptr;
    ret.assign("--");
    EQ(ret, "--");
    db.get_entry_value_by_tags(s3, &cret);
    NOPRINT_NOTEQ(cret, nullptr);
    ret.assign(cret);
    free(cret);
    EQ(ret, "X");

    TEST_PASS
}

TEST(get_entry_value_by_tags_fail) {
    SeqDB db;
    DBEntry<> entry1;
    entry1.add_tag("test1").add_tag("test2").add_tag("test3").add_tag("test4");
    entry1.add_to_value("entry1value");
    DBEntry<> entry2;
    entry2.add_tag("test1").add_tag("test2").add_tag("test3").add_tag("test4");
    entry2.add_to_value("entry2value");
    db.add_entry(entry1);
    db.add_entry(entry2);
    EQ(db.size(), 2);

    const char* tags[] = {"testX", "test1", "test2", ""};
    char* cret;
    cret = (char*)1234;
    db.get_entry_value_by_tags(tags, &cret);
    NOPRINT_EQ(cret, nullptr);

    const char* notags[] = {""};
    cret = (char*)1234;
    db.get_entry_value_by_tags(notags, &cret);
    NOPRINT_EQ(cret, nullptr);

    TEST_PASS
}

TEST(get_entry_value_by_key) {
    SeqDB db;
    {
        dbkey_t key {pack_chain_info(BTC_KEY, TX_IN_EDGE_KEY, 0), 1, UTXO_KEY};
        DBEntry<> e; e.set_key(key); e.value() = "test1"; db.add_entry(move(e));
    }
    {
        dbkey_t key {pack_chain_info(ZEC_KEY, TX_OUT_EDGE_KEY, 1), 2, NOT_UTXO_KEY};
        DBEntry<> e; e.set_key(key); e.value() = "test2"; db.add_entry(move(e));
    }

    dbkey_t search_key {pack_chain_info(BTC_KEY, TX_IN_EDGE_KEY, 0), 1, UTXO_KEY};
    char* val;
    db.get_entry_value_by_key(search_key, &val);
    NOPRINT_NOTEQ(val, nullptr);
    string ret_val; ret_val.assign(val);
    EQ(ret_val, "test1");
    free(val);

    dbkey_t search_key2 {pack_chain_info(ZEC_KEY, TX_OUT_EDGE_KEY, 1), 2, NOT_UTXO_KEY};
    char* val2;
    db.get_entry_value_by_key(search_key2, &val2);
    NOPRINT_NOTEQ(val2, nullptr);
    string ret_val2; ret_val2.assign(val2);
    EQ(ret_val2, "test2");
    free(val2);

    TEST_PASS
}

TEST(get_entry_by_tags_access) {
    SeqDB db;
    DBEntry<> entry1;
    entry1.add_tag("test1").add_tag("test2").add_tag("test3").add_tag("test4");
    entry1.add_to_value("entry1value");
    DBEntry<> entry2;
    entry2.add_tag("test1").add_tag("test2").add_tag("test3").add_tag("test4");
    entry2.add_to_value("entry2value");
    db.add_entry(entry1);
    db.add_entry(entry2);
    EQ(db.size(), 2);

    DBEntry<> entry3;
    entry3.add_tag("test1");
    entry3.add_to_value("entry3value");
    db.add_entry(entry3);

    EQ(db.size(), 3);

    auto acc = entry3.access();
    db.add_db_access(acc);

    const char* tags[] = {"test1", "test2", ""};
    char* cret;
    acc->get_entry_by_tags.run(&acc->get_entry_by_tags, tags, &cret);
    NOPRINT_NOTEQ(cret, nullptr);
    string ret { cret };
    free(cret);

    bool success = false;
    if (ret == "entry1value\n" || ret == "entry2value\n") success = true;
    EQ(success, true);

    delete acc;

    TEST_PASS
}

static int g_TEST_install_filter_fi_pass;
bool g_TEST_install_filter_fi_sroe([[maybe_unused]] const DBAccess* acc) {
    auto tags = acc->tags;
    while (*tags[0] != '\0') {
        if (*tags[0] == 'A') return true;
        ++tags;
    }
    return false;
}
void g_TEST_install_filter_fi_run([[maybe_unused]] void* access) {
    ++g_TEST_install_filter_fi_pass;
}

TEST(install_filter_fi) {
    g_TEST_install_filter_fi_pass = 0;
    FilterInterface i {
        filter_name: "TEST",
        filter_type: SINGLE_ENTRY,
        should_run: &g_TEST_install_filter_fi_sroe,
        init: nullptr,
        destroy: nullptr,
        run: &g_TEST_install_filter_fi_run
    };

    SeqDB d;
    { DBEntry<> e; e.clear().add_tag("A"); d.add_entry(e); }
    { DBEntry<> e; e.clear().add_tag("B"); d.add_entry(e); }
    { DBEntry<> e; e.clear().add_tag("A", "C"); d.add_entry(e); }
    { DBEntry<> e;  e.clear().add_tag("D", "C"); d.add_entry(e); }
    { DBEntry<> e; e.clear().add_tag("E"); d.add_entry(e); }
    { DBEntry<> e; e.clear().add_tag("A"); d.add_entry(e); }

    filter_p fp = make_shared<Filter>(&i);

    d.install_filter(fp);
    EQ(g_TEST_install_filter_fi_pass, 0);
    d.process_once();
    EQ(g_TEST_install_filter_fi_pass, 3);

    TEST_PASS
}

TEST(remove_tag_from_entry) {
    SeqDB db;
    DBEntry<> e;
    e.clear().add_tag("A", "B", "C"); db.add_entry(e);

    const char* search_tags[] = {"A", "B", "C", ""};
    db.remove_tag_from_entry(search_tags, "C");

    bool correct = false;
    for (auto & [key, entry] : db.entries()) {
        if (entry.has_tag("A") && entry.has_tag("B")) {
            if (!entry.has_tag("C"))
                correct = true;
        }

    }

    EQ(correct, true);

    TEST_PASS
}

TEST(add_tag_later) {
    SeqDB db;

    DBEntry<> e2_copy;

    dbkey_t key1 {1,1,1};
    dbkey_t key2 {2,2,2};
    { DBEntry<> e; e.clear().add_tag("a", "b").value() = "e1"; e.set_key(key1); db.add_entry(move(e)); }
    { DBEntry<> e; e.clear().add_tag("c").value() = "e2"; e2_copy = e; e.set_key(key2); db.add_entry(move(e)); }

    char* val = nullptr;
    db.get_entry_value_by_tags(e2_copy.c_tags(), &val);
    NOPRINT_NOTEQ(val, nullptr);
    EQ(string{val}, "e2");
    free(val); val = nullptr;

    auto entries = db.entries();
    EQ(entries.size(), 2);

    DBEntry<>* e1_ptr = nullptr;
    for (auto & [key, entry] : entries) {
        if (entry.value() == "e1") {
            e1_ptr = &entry;
            break;
        }
    }
    NOPRINT_NOTEQ(e1_ptr, nullptr);
    DBEntry<>& e1 = *e1_ptr;
    EQ(e1.value(), "e1");

    db.get_entry_value_by_tags(e1.c_tags(), &val);
    NOPRINT_NOTEQ(val, nullptr);
    EQ(string{val}, "e1");
    free(val); val = nullptr;

    DBEntry<> e1_copy = e1;

    // Make sure that it will NOT find by the tag 'test' when the tag is only
    // added to the entry, not the DB
    e1.add_tag("test");
    EQ(e1.has_tag("test"), true);
    db.get_entry_value_by_tags(e1.c_tags(), &val);
    NOPRINT_EQ(val, nullptr);

    // But make sure it does find it when added through the DB
    db.add_tag_to_entry(e1.get_key(), "test");
    e1 = move(db.entries()[key1]);
    EQ(e1.has_tag("test"), true);
    db.get_entry_value_by_tags(e1.c_tags(), &val);
    NOPRINT_NOTEQ(val, nullptr);
    EQ(string{val}, "e1");
    free(val); val = nullptr;

    // Do it again with a new tag, and make sure that the tag is also added to
    // the e1 entry reference
    db.add_tag_to_entry(e1.get_key(), "test2", "test3", "test4");
    e1 = move(db.entries()[key1]);
    EQ(e1.has_tag("test2"), true);
    EQ(e1.has_tag("test3"), true);
    EQ(e1.has_tag("test4"), true);
    db.get_entry_value_by_tags(e1.c_tags(), &val);

    NOPRINT_NOTEQ(val, nullptr);
    EQ(string{val}, "e1");
    free(val); val = nullptr;

    db.stage_close();
    e1_copy = e1;

    // Now delete tags from e1
    e1.remove_tag("test2", "test3");
    db.get_entry_value_by_tags(e1_copy.c_tags(), &val);
    NOPRINT_NOTEQ(val, nullptr);
    EQ(string{val}, "e1");
    free(val); val = nullptr;

    // Finally, make sure the index updates
    e1.add_tag("test2", "test3");
    db.remove_tag_from_entry(e1.get_key(), "test2", "test3");
    db.get_entry_value_by_tags(e1_copy.c_tags(), &val);
    NOPRINT_EQ(val, nullptr);

    TEST_PASS
}

TEST(clear_filters) {
    SeqDB db;
    db.add_filter_dir(build_dir + "/filters");
    db.install_filter("BTC_block_to_tx");
    EQ(db.num_filters(), 1);
    db.install_filter("BTC_tx_in_edges");
    EQ(db.num_filters(), 2);
    db.clear_filters();
    EQ(db.num_filters(), 0);

    TEST_PASS
}

TEST(subscribe_to_entry) {
    SeqDB db;

    DBEntry<> my_entry;
    dbkey_t my_key = {0,0,0};
    my_entry.set_key(my_key);
    my_entry.value() = "testing";
    my_entry.add_tag("foo:inactive");
    db.add_entry(move(my_entry));

    DBEntry<> wait_entry;
    dbkey_t wait_key = {1,1,1};
    wait_entry.set_key(wait_key);
    wait_entry.value() = "";

    db.subscribe_to_entry(my_key, wait_key, "foo:inactive");

    db.add_entry(move(wait_entry));
    db.stage_close();

    bool entry_found = false;
    for (auto& [key, entry] : db.entries()) {
        if (key == my_key) {
            EQ(entry_found, false);
            entry_found = true;
            EQ(entry.has_tag("foo:inactive"), false);
        }
    }

    EQ(entry_found, true);

    TEST_PASS
}

TEST(concat_stage) {
    SeqDB db;

    DBEntry<> e1; e1.value() = "foo";
    chain_info_t ci1 = pack_chain_info(10,10,10);
    dbkey_t key1 = {ci1, 10, 10};
    e1.set_key(key1);
    db.stage_add_entry(move(e1));

    DBEntry<> e2; e2.value() = "bar";
    chain_info_t ci2 = pack_chain_info(10,10,10);
    dbkey_t key2 = {ci2, 10, 10};
    e2.set_key(key2);
    db.stage_add_entry(move(e2));

    EQ(key1, key2);

    db.stage_close();

    bool found = false;
    for (auto &[key, entry] : db.entries()) {
        if (entry.value() == "foo\nbar") {
            EQ(found, false);
            found = true;
        }
    }

    EQ(found, true);

    TEST_PASS
}
TEST(concat_previously_exist) {
    SeqDB db;

    DBEntry<> e1; e1.value() = "foo";
    chain_info_t ci1 = pack_chain_info(10,10,10);
    dbkey_t key1 = {ci1, 10, 10};
    e1.set_key(key1);
    db.stage_add_entry(move(e1));

    db.stage_close();

    DBEntry<> e2; e2.value() = "bar";
    chain_info_t ci2 = pack_chain_info(10,10,10);
    dbkey_t key2 = {ci2, 10, 10};
    e2.set_key(key2);
    db.stage_add_entry(move(e2));

    db.stage_close();

    bool found = false;
    for (auto &[key, entry] : db.entries()) {
        if (entry.value() == "foo\nbar") {
            EQ(found, false);
            found = true;
        }
    }

    EQ(found, true);

    TEST_PASS
}

TEST(update_val) {
    SeqDB db;
    dbkey_t key = {10,10,10};
    {
        DBEntry<> e; e.value() = "foo"; e.set_key(key);
        db.add_entry(move(e));
    }
    EQ(db.size(), 1);
    db.stage_update_entry_val(key, "bar");
    EQ(db.size(), 1);
    for (auto &[key, entry] : db.entries()) {
        EQ(entry.value(), "foo");
    }
    db.stage_close();
    EQ(db.size(), 1);
    for (auto &[key, entry] : db.entries()) {
        EQ(entry.value(), "bar");
    }

    TEST_PASS
}

TEST(serialize_entries) {
    //SeqDB db { data_dir+"/simple_bitcoin.txt" };
    SeqDB db;
    
    dbkey_t key1 {0, 0, 0};
    dbkey_t key2 {1, 0, 0};
    {
        DBEntry e;
        e.set_key(key1);
        e.value() = "test1";
        e.add_tag("tag1", "tag2");
        db.add_entry(move(e));
    }
    {
        DBEntry e;
        e.set_key(key2);
        e.value() = "test2";
        e.add_tag("tag3", "tag4");
        db.add_entry(move(e));
    }

    EQ(db.size(), 2);

    size_t msg_size = db.serialize_size();
    char* msg = new char[msg_size];
    char *msg_ptr = msg;
    db.serialize_entries(msg_ptr);

    msg_ptr = msg;
    auto entries_vec = SeqDB::deserialize_entries(msg_ptr);

    delete[] msg;

    EQ(entries_vec.size(), 2);

    bool e1_found = false;
    bool e2_found = false;
    for (auto & [key, entry] : db.entries()) {
        if (entry.has_tag("tag1")) {
            EQ(e1_found, false);
            EQ(entry.has_tag("tag2"), true);
            EQ(entry.value(), "test1");
            EQ(entry.get_key(), key1);
            e1_found = true;
        }
        if (entry.has_tag("tag3")) {
            EQ(e2_found, false);
            EQ(entry.has_tag("tag4"), true);
            EQ(entry.value(), "test2");
            EQ(entry.get_key(), key2);
            e2_found = true;
        }
    }

    EQ(e1_found, true);
    EQ(e2_found, true);

    TEST_PASS
}

TEST(key_collision) {
    RandomKeyGen r {100};

    set<dbkey_t> keys;

    size_t num_entries = 5000000;
    for (size_t i = 0; i < num_entries; i++) {
        keys.insert(r.get());
    }

    EQ(keys.size(), num_entries);

    TEST_PASS
}

TEST(random_key) {
    SeqDB db;
    {DBEntry<> e; e.value() = "foo"; db.add_entry(move(e));}
    {DBEntry<> e; e.value() = "foo"; db.add_entry(move(e));}

    EQ(db.size(), 2);

    bool entry_found = false;
    for (auto & [key, entry]: db.entries()) {
        EQ(entry.random_key(), true);
        entry_found = true;
    }
    EQ(entry_found, true);

    TEST_PASS
}

TEST(random_key_stage_close) {
    SeqDB db;
    {
        DBEntry<> e; e.value() = "foo";
        EQ(e.get_key(), INITIAL_KEY);
        db.stage_add_entry(move(e));
    }
    {
        DBEntry<> e; e.value() = "foo";
        EQ(e.get_key(), INITIAL_KEY);
        db.stage_add_entry(move(e));
    }

    EQ(db.size(), 0)
    db.stage_close();
    EQ(db.size(), 2)

    bool entry_found = false;
    for (auto & [key, entry]: db.entries()) {
        EQ(entry.random_key(), true);
        entry_found = true;
    }
    EQ(entry_found, true);

    TEST_PASS
}

TEST(import_export) {
    vector<dbkey_t> keys;
    const string fn = "exported";
    dbkey_t set_key {1,2,3};
    {
        SeqDB db { data_dir+"/simple_bitcoin.txt" };

        DBEntry<> e; e.add_tag("a", "b").value() = "res "; e.set_key(set_key); db.add_entry(move(e));
        // Ensure the number of entries is correct
        EQ(db.size(), 3);
        keys = db.get_keys();

        db.export_db(fn);
    }

    SeqDB db2;
    db2.import_db(fn);

    EQ(db2.size(), 3);

    set<dbkey_t> orig_keys;
    orig_keys.insert(keys.begin(), keys.end());

    size_t found_count = 0;
    bool keyed_entry_found = false;
    auto keys2 = db2.get_keys();
    EQ(keys2.size(), keys.size());
    for (auto & [key, entry] : db2.entries()) {
        if (key == set_key) {
            EQ(keyed_entry_found, false);
            EQ(entry.value(), "res ");
            EQ(entry.has_tag("a"), true);
            EQ(entry.has_tag("b"), true);
            EQ(entry.tags().size(), 2);
            keyed_entry_found = true;
        } else if (orig_keys.count(key) == 0) {
            TEST_FAIL
        } else {
            ++found_count;
        }
    }

    EQ(found_count, 2);
    EQ(keyed_entry_found, true);

    remove(fn.c_str());

    TEST_PASS
}

TEST(import_export_with_tag) {
    const string fn = "exported";
    dbkey_t set_key {1,2,3};
    {
        SeqDB db { data_dir+"/simple_bitcoin.txt" };

        DBEntry<> e; e.add_tag("a", "b").value() = "res "; e.set_key(set_key); db.add_entry(move(e));
        // Ensure the number of entries is correct
        EQ(db.size(), 3);

        db.export_db_with_tag(fn, "a");
    }

    SeqDB db2;
    db2.import_db(fn);

    EQ(db2.size(), 1);

    auto keys = db2.keys();
    dbkey_t single_key = *keys.begin();
    EQ(single_key, set_key);

    remove(fn.c_str());

    TEST_PASS
}

TESTS_BEGIN
    elga::ZMQChatterbox::Setup();

    RUN_TEST(bad_file)
    RUN_TEST(open_close)
    RUN_TEST(correct_tags)
    RUN_TEST(correct_c_tags)
    RUN_TEST(correct_values)
    RUN_TEST(modify_entries)
    RUN_TEST(dup_filters_fail)
    RUN_TEST(bad_filter_dir)
    RUN_TEST(install_filter_fail)
    RUN_TEST(copy_entry)
    RUN_TEST(move_entry)
    RUN_TEST(clear)
    RUN_TEST(stage_entries)
    RUN_TEST(access)
    RUN_TEST(access_tags)
    RUN_TEST(get_entry_by_tags)
    RUN_TEST(get_entry_value_by_tags)
    RUN_TEST(get_entry_value_by_tags_fail)
    RUN_TEST(get_entry_value_by_key)
    RUN_TEST(get_entry_by_tags_access)
    RUN_TEST(install_filter_fi)
    RUN_TEST(remove_tag_from_entry)
    RUN_TEST(add_tag_later)
    RUN_TEST(clear_filters)
    RUN_TEST(subscribe_to_entry)
    RUN_TEST(concat_stage)
    RUN_TEST(concat_previously_exist)
    RUN_TEST(update_val)
    RUN_TEST(key_collision)
    RUN_TEST(random_key)
    RUN_TEST(random_key_stage_close)
    RUN_TEST(serialize_entries)
    RUN_TEST(import_export)
    RUN_TEST(import_export_with_tag)

    elga::ZMQChatterbox::Teardown();
TESTS_END
