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

// Filter for the max_filters_per_stage tests: runs on entries tagged "A" that
// it hasn't tagged "A:done" yet, so each entry is filtered exactly once
static size_t g_TEST_max_filters_runs;
bool g_TEST_max_filters_should_run(const DBAccess* acc) {
    bool found_a = false;
    for (auto tags = acc->tags; *tags[0] != '\0'; ++tags) {
        if (string(*tags) == "A:done") return false;
        if (string(*tags) == "A") found_a = true;
    }
    return found_a;
}
void g_TEST_max_filters_run(void* access) {
    const DBAccess* acc = (const DBAccess*)access;
    ++g_TEST_max_filters_runs;
    acc->add_tag.run(&acc->add_tag, "A:done");
}

TEST(max_filters_per_stage) {
    g_TEST_max_filters_runs = 0;
    FilterInterface i {
        filter_name: "TEST_max_filters",
        filter_type: SINGLE_ENTRY,
        should_run: &g_TEST_max_filters_should_run,
        init: nullptr,
        destroy: nullptr,
        run: &g_TEST_max_filters_run
    };

    SeqDB d;
    EQ(d.max_filters_per_stage(), SeqDB::UNLIMITED_FILTERS_PER_STAGE);
    for (int k = 0; k < 5; ++k) { DBEntry<> e; e.clear().add_tag("A"); d.add_entry(e); }
    { DBEntry<> e; e.clear().add_tag("B"); d.add_entry(e); }

    d.set_max_filters_per_stage(2);
    EQ(d.max_filters_per_stage(), 2);
    d.install_filter(make_shared<Filter>(&i));

    // Each pass runs at most 2 filters; the "A:done" tags are applied when
    // the stage closes, so the next pass picks up the remaining entries
    EQ(d.process_once(), true);
    EQ(g_TEST_max_filters_runs, 2);
    EQ(d.process_once(), true);
    EQ(g_TEST_max_filters_runs, 4);
    EQ(d.process_once(), true);
    EQ(g_TEST_max_filters_runs, 5);
    EQ(d.process_once(), false);
    EQ(g_TEST_max_filters_runs, 5);

    size_t done = 0;
    for (auto & [key, entry] : d.entries())
        if (entry.has_tag("A:done")) ++done;
    EQ(done, 5);

    TEST_PASS
}

TEST(max_filters_per_stage_rejects_zero) {
    SeqDB d;
    bool threw = false;
    try {
        d.set_max_filters_per_stage(0);
    } catch (const invalid_argument&) {
        threw = true;
    }
    EQ(threw, true);
    EQ(d.max_filters_per_stage(), SeqDB::UNLIMITED_FILTERS_PER_STAGE);

    TEST_PASS
}

// With the default (unlimited) max_filters_per_stage, one pass runs the filter
// on every qualifying entry instead of stopping early
TEST(max_filters_per_stage_default_unlimited) {
    g_TEST_max_filters_runs = 0;
    FilterInterface i {
        filter_name: "TEST_max_filters",
        filter_type: SINGLE_ENTRY,
        should_run: &g_TEST_max_filters_should_run,
        init: nullptr,
        destroy: nullptr,
        run: &g_TEST_max_filters_run
    };

    SeqDB d;
    const size_t n = 1000;
    for (size_t k = 0; k < n; ++k) { DBEntry<> e; e.clear().add_tag("A"); d.add_entry(e); }
    d.install_filter(make_shared<Filter>(&i));

    EQ(d.process_once(), true);
    EQ(g_TEST_max_filters_runs, n);
    EQ(d.process_once(), false);
    EQ(g_TEST_max_filters_runs, n);

    TEST_PASS
}

// Filters for the scan cursor tests. tag_other runs on entries tagged "Y" and
// adds "TagB" to the entry at {a+1, b, c} (a new entry with that key merges
// into it). on_tag_b runs once on each entry tagged "TagB".
static size_t g_TEST_on_tag_b_runs;
static bool g_TEST_has_tag(const DBAccess* acc, const string& want) {
    for (auto tags = acc->tags; *tags[0] != '\0'; ++tags)
        if (string(*tags) == want) return true;
    return false;
}
bool g_TEST_tag_other_should_run(const DBAccess* acc) {
    return g_TEST_has_tag(acc, "Y") && !g_TEST_has_tag(acc, "tag_other:done");
}
void g_TEST_tag_other_run(void* access) {
    const DBAccess* acc = (const DBAccess*)access;
    dbkey_t target {acc->key.a + 1, acc->key.b, acc->key.c};
    const char* new_tags[] = {"TagB", ""};
    acc->make_new_entry.run(&acc->make_new_entry, new_tags, "", target);
    acc->add_tag.run(&acc->add_tag, "tag_other:done");
}
bool g_TEST_on_tag_b_should_run(const DBAccess* acc) {
    return g_TEST_has_tag(acc, "TagB") && !g_TEST_has_tag(acc, "on_tag_b:done");
}
void g_TEST_on_tag_b_run(void* access) {
    const DBAccess* acc = (const DBAccess*)access;
    ++g_TEST_on_tag_b_runs;
    acc->add_tag.run(&acc->add_tag, "on_tag_b:done");
}

// A filter made runnable on an entry the scan has already passed (here by
// another filter tagging it) must still run before processing reports done
TEST(cursor_runs_entries_behind_cursor) {
    g_TEST_on_tag_b_runs = 0;
    FilterInterface tag_other {
        filter_name: "TEST_tag_other",
        filter_type: SINGLE_ENTRY,
        should_run: &g_TEST_tag_other_should_run,
        init: nullptr,
        destroy: nullptr,
        run: &g_TEST_tag_other_run
    };
    FilterInterface on_tag_b {
        filter_name: "TEST_on_tag_b",
        filter_type: SINGLE_ENTRY,
        should_run: &g_TEST_on_tag_b_should_run,
        init: nullptr,
        destroy: nullptr,
        run: &g_TEST_on_tag_b_run
    };

    // Pairs: Y_i at {20, i, 0} tags X_i at {21, i, 0}. Scan order is the
    // hash order, so about half the X_i come before their Y_i
    size_t pairs = 20;
    SeqDB d;
    for (size_t k = 0; k < pairs; ++k) {
        { DBEntry<> e; e.clear().add_tag("X").value() = "x"; e.set_key({21, (vtx_t)k, 0}); d.add_entry(move(e)); }
        { DBEntry<> e; e.clear().add_tag("Y").value() = "y"; e.set_key({20, (vtx_t)k, 0}); d.add_entry(move(e)); }
    }
    d.set_max_filters_per_stage(1);
    d.install_filter(make_shared<Filter>(&tag_other));
    d.install_filter(make_shared<Filter>(&on_tag_b));

    size_t passes = 0;
    while (d.process_once()) {
        ++passes;
        if (passes > 10*pairs) TEST_FAIL
    }

    EQ(g_TEST_on_tag_b_runs, pairs);
    size_t done = 0;
    for (auto & [key, entry] : d.entries())
        if (entry.has_tag("X") && entry.has_tag("on_tag_b:done")) ++done;
    EQ(done, pairs);

    // Without the cursor every pass rescans from the first key (about 40
    // passes x 20 entries); with it, a few laps of 40
    EQ(d.entries_examined() <= 6*2*pairs, true);

    TEST_PASS
}

// The scan resumes where the last pass stopped instead of starting over
TEST(cursor_scan_cost) {
    g_TEST_max_filters_runs = 0;
    FilterInterface i {
        filter_name: "TEST_max_filters",
        filter_type: SINGLE_ENTRY,
        should_run: &g_TEST_max_filters_should_run,
        init: nullptr,
        destroy: nullptr,
        run: &g_TEST_max_filters_run
    };

    SeqDB d;
    for (int k = 0; k < 10; ++k) { DBEntry<> e; e.clear().add_tag("A"); d.add_entry(e); }
    for (int k = 0; k < 90; ++k) { DBEntry<> e; e.clear().add_tag("B"); d.add_entry(e); }
    d.set_max_filters_per_stage(2);
    d.install_filter(make_shared<Filter>(&i));

    while (d.process_once()) {}
    EQ(g_TEST_max_filters_runs, 10);

    // One lap doing the work plus one clean lap confirming it is done
    size_t examined = d.entries_examined();
    EQ(examined <= 2*100, true);

    // Once done, another pass runs nothing and reports done again after one lap
    EQ(d.process_once(), false);
    EQ(g_TEST_max_filters_runs, 10);
    EQ(d.entries_examined(), examined + 100);

    TEST_PASS
}

// ---------------------------------------------------------------------------
// Memory policy (memory_policy.hpp): ending passes early to bound memory

static const uint64_t GiB = 1ull << 30;

// The max_filters filter: runs once on each "A" entry, adding "A:done"
static FilterInterface g_TEST_a_done_fi {
    filter_name: "TEST_max_filters",
    filter_type: SINGLE_ENTRY,
    should_run: &g_TEST_max_filters_should_run,
    init: nullptr,
    destroy: nullptr,
    run: &g_TEST_max_filters_run
};
// What one run of it emits: one tag add
static const size_t g_A_DONE_BYTES = sizeof(dbkey_t) + strlen("A:done");

static void add_a_entries(SeqDB& d, size_t n) {
    for (size_t k = 0; k < n; ++k) { DBEntry<> e; e.clear().add_tag("A"); d.add_entry(e); }
}
static size_t count_a_done(SeqDB& d) {
    size_t done = 0;
    for (auto & [key, entry] : d.entries())
        if (entry.has_tag("A:done")) ++done;
    return done;
}

// The policy is on by default: with plenty of memory, a pass gets a quarter
// of the headroom above the 90% limit as its budget, and runs to the end
TEST(memory_policy_on_by_default) {
    g_TEST_max_filters_runs = 0;
    SeqDB d;
    EQ(d.memory_policy().enabled(), true);
    EQ(d.agents_per_node(), 1);
    MemInfo mem;
    mem.ok = true;
    mem.total_bytes = 100*GiB;
    mem.available_bytes = 50*GiB;
    d.set_meminfo_source([&]() { return mem; });
    add_a_entries(d, 5);
    d.install_filter(make_shared<Filter>(&g_TEST_a_done_fi));

    EQ(d.process_once(), true);
    EQ(g_TEST_max_filters_runs, 5);
    auto& p1 = d.stats().history[0];
    EQ(p1.stop_reason, STOP_NONE);
    EQ(p1.bytes_emitted, 5*g_A_DONE_BYTES);
    // reserve 10 GiB, headroom 40 GiB, a quarter of it (to within rounding
    // of the 10%)
    EQ(p1.byte_budget / (1u << 20), 10*1024);
    EQ(d.under_memory_pressure(), false);

    TEST_PASS
}

// Disabled, nothing changes and memory is never read, but output is still
// counted (pando_top shows it)
TEST(memory_policy_disabled) {
    g_TEST_max_filters_runs = 0;
    SeqDB d;
    d.set_memory_policy(MemoryPolicy::disabled());
    EQ(d.memory_policy().enabled(), false);
    size_t meminfo_reads = 0;
    d.set_meminfo_source([&]() { ++meminfo_reads; return MemInfo{}; });
    add_a_entries(d, 5);
    d.install_filter(make_shared<Filter>(&g_TEST_a_done_fi));

    EQ(d.process_once(), true);
    EQ(g_TEST_max_filters_runs, 5);
    EQ(d.process_once(), false);

    auto& p1 = d.stats().history[0];
    EQ(p1.stopped_early, false);
    EQ(p1.stop_reason, STOP_NONE);
    EQ(p1.byte_budget, NO_BYTE_BUDGET);
    EQ(p1.bytes_emitted, 5*g_A_DONE_BYTES);
    EQ(d.stats().history[1].bytes_emitted, 0);
    EQ(meminfo_reads, 0);
    EQ(d.under_memory_pressure(), false);

    TEST_PASS
}

TEST(memory_policy_rejects_invalid) {
    SeqDB d;
    auto rejected = [&](auto modify) {
        MemoryPolicy p;
        p.pass_budget_bytes = 1;
        modify(p);
        try {
            d.set_memory_policy(p);
        } catch (const invalid_argument&) {
            return true;
        }
        return false;
    };
    EQ(rejected([](MemoryPolicy& p) { p.budget_fraction = 0; }), true);
    EQ(rejected([](MemoryPolicy& p) { p.budget_fraction = 1.5; }), true);
    EQ(rejected([](MemoryPolicy& p) { p.limit_pct = -1; }), true);
    EQ(rejected([](MemoryPolicy& p) { p.limit_pct = 101; }), true);
    EQ(rejected([](MemoryPolicy& p) { p.check_interval = -chrono::milliseconds(1); }), true);
    // A rejected policy leaves the old one (the default) in place
    EQ(d.memory_policy().pass_budget_bytes, 0);
    FEQ(d.memory_policy().limit_pct, 90);
    EQ(rejected([](MemoryPolicy&) { }), false);
    EQ(d.memory_policy().pass_budget_bytes, 1);

    TEST_PASS
}

// A fixed byte budget ends each pass once its filters have emitted that much;
// the cursor carries over, so nothing is skipped or run twice
TEST(mem_budget_stops_pass) {
    g_TEST_max_filters_runs = 0;
    SeqDB d;
    size_t meminfo_reads = 0;
    d.set_meminfo_source([&]() { ++meminfo_reads; return MemInfo{}; });
    MemoryPolicy p = MemoryPolicy::disabled();
    p.pass_budget_bytes = 3*g_A_DONE_BYTES;
    d.set_memory_policy(p);
    add_a_entries(d, 10);
    d.install_filter(make_shared<Filter>(&g_TEST_a_done_fi));

    vector<size_t> runs;
    while (d.process_once()) {
        runs.push_back(g_TEST_max_filters_runs);
        if (runs.size() > 20) TEST_FAIL
    }
    NOPRINT_EQ(runs, (vector<size_t>{3, 6, 9, 10}));
    EQ(count_a_done(d), 10);

    auto& h = d.stats().history;
    EQ(h.size(), 5);
    for (size_t i = 0; i < 3; ++i) {
        EQ(h[i].stopped_early, true);
        EQ(h[i].stop_reason, STOP_MEM_BUDGET);
        EQ(h[i].bytes_emitted, 3*g_A_DONE_BYTES);
        EQ(h[i].byte_budget, 3*g_A_DONE_BYTES);
    }
    // The last entry is under budget and ends the lap
    EQ(h[3].stopped_early, false);
    EQ(h[3].stop_reason, STOP_NONE);
    EQ(h[3].bytes_emitted, g_A_DONE_BYTES);
    // A fixed budget alone never reads system memory
    EQ(meminfo_reads, 0);

    TEST_PASS
}

// The case a plain "memory above X% => stop" rule gets wrong: memory is
// already over the limit when processing starts. The budget is then 0, but
// every pass still runs one filter before it stops, so processing finishes,
// one entry per pass.
TEST(mem_high_at_start_still_progresses) {
    g_TEST_max_filters_runs = 0;
    SeqDB d;
    MemInfo mem;
    mem.ok = true;
    mem.total_bytes = 100*GiB;
    mem.available_bytes = 10*GiB;      // 90% used
    d.set_meminfo_source([&]() { return mem; });
    MemoryPolicy p;
    p.limit_pct = 75;
    p.check_interval = chrono::milliseconds(0);
    d.set_memory_policy(p);
    add_a_entries(d, 10);
    d.install_filter(make_shared<Filter>(&g_TEST_a_done_fi));

    vector<size_t> runs;
    while (d.process_once()) {
        runs.push_back(g_TEST_max_filters_runs);
        if (runs.size() > 20) TEST_FAIL
    }
    NOPRINT_EQ(runs, (vector<size_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
    EQ(count_a_done(d), 10);

    auto& h = d.stats().history;
    EQ(h.size(), 11);
    for (size_t i = 0; i < 9; ++i) {
        EQ(h[i].byte_budget, 0);
        EQ(h[i].stop_reason, STOP_MEM_BUDGET);
        EQ(h[i].stopped_early, true);
        EQ(h[i].bytes_emitted, g_A_DONE_BYTES);
    }
    // The 10th run was on the last key of the lap: the budget check still
    // fired, but the pass was not cut short
    EQ(h[9].stop_reason, STOP_MEM_BUDGET);
    EQ(h[9].stopped_early, false);
    // Nothing left to run, so the zero budget never stopped the last pass
    EQ(h[10].stop_reason, STOP_NONE);
    EQ(h[10].bytes_emitted, 0);
    EQ(d.under_memory_pressure(), true);

    TEST_PASS
}

// Memory rising during a pass (here 1 GiB per filter run, freed when the
// stage closes) ends it as soon as it goes over the limit, even with plenty
// of byte budget left
TEST(mem_pressure_stops_pass) {
    g_TEST_max_filters_runs = 0;
    SeqDB d;
    size_t runs_at_pass_start = 0;
    bool force_low = false;
    d.set_meminfo_source([&]() {
        MemInfo m;
        m.ok = true;
        m.total_bytes = 100*GiB;
        uint64_t used_this_pass = (g_TEST_max_filters_runs - runs_at_pass_start) * GiB;
        m.available_bytes = force_low ? 20*GiB : 28*GiB - used_this_pass;
        return m;
    });
    MemoryPolicy p;
    p.limit_pct = 75;                   // reserve 25 GiB
    p.budget_fraction = 0.5;
    p.check_interval = chrono::milliseconds(0);
    d.set_memory_policy(p);
    add_a_entries(d, 10);
    d.install_filter(make_shared<Filter>(&g_TEST_a_done_fi));

    vector<size_t> runs;
    while (true) {
        runs_at_pass_start = g_TEST_max_filters_runs;
        if (!d.process_once()) break;
        runs.push_back(g_TEST_max_filters_runs);
        if (runs.size() > 20) TEST_FAIL
    }
    // 28 GiB available at the start: 27, 26, 25 (at the limit, not over),
    // then 24 after the 4th run
    NOPRINT_EQ(runs, (vector<size_t>{4, 8, 10}));
    EQ(count_a_done(d), 10);

    auto& h = d.stats().history;
    EQ(h[0].stop_reason, STOP_MEM_PRESSURE);
    EQ(h[0].stopped_early, true);
    // budget = 0.5 * (28 - 25) GiB / 1 agent; far more than was emitted
    EQ(h[0].byte_budget, 3*GiB/2);
    EQ(h[0].bytes_emitted, 4*g_A_DONE_BYTES);
    EQ(h[1].stop_reason, STOP_MEM_PRESSURE);
    EQ(h[2].stop_reason, STOP_NONE);

    // The pressure check between passes uses the last pass's reserve
    runs_at_pass_start = g_TEST_max_filters_runs;
    EQ(d.under_memory_pressure(), false);
    force_low = true;
    EQ(d.under_memory_pressure(), true);

    TEST_PASS
}

// The headroom budget is computed when each pass starts; a fixed budget
// caps it; and if memory cannot be read, only the fixed budget applies
TEST(mem_budget_from_headroom) {
    g_TEST_max_filters_runs = 0;
    SeqDB d;
    MemInfo mem;
    mem.ok = true;
    mem.total_bytes = 100*GiB;
    mem.available_bytes = 55*GiB;
    d.set_meminfo_source([&]() { return mem; });
    MemoryPolicy p;
    p.limit_pct = 75;                   // reserve 25 GiB, headroom 30 GiB
    p.budget_fraction = 0.5;
    p.agents_per_node = 3;
    d.set_memory_policy(p);
    add_a_entries(d, 4);
    d.install_filter(make_shared<Filter>(&g_TEST_a_done_fi));

    d.process_once();
    EQ(d.stats().history.back().byte_budget, 5*GiB);

    // Less headroom next pass, smaller budget
    mem.available_bytes = 31*GiB;
    d.process_once();
    EQ(d.stats().history.back().byte_budget, 1*GiB);

    // A smaller fixed budget wins
    p.pass_budget_bytes = 1000;
    d.set_memory_policy(p);
    d.process_once();
    EQ(d.stats().history.back().byte_budget, 1000);

    // Memory unreadable: the fixed budget still applies, no pressure check
    mem.ok = false;
    d.process_once();
    EQ(d.stats().history.back().byte_budget, 1000);
    EQ(d.under_memory_pressure(), false);
    p.pass_budget_bytes = 0;
    d.set_memory_policy(p);
    d.process_once();
    EQ(d.stats().history.back().byte_budget, NO_BYTE_BUDGET);

    // agents_per_node = 0 asks the source (ParDB's: Slurm, else the mesh),
    // each pass; no source, or an answer of 0, means 1
    mem.ok = true;
    mem.available_bytes = 55*GiB;
    p.agents_per_node = 0;
    d.set_memory_policy(p);
    d.process_once();
    EQ(d.stats().history.back().byte_budget, 15*GiB);
    uint64_t on_node = 6;
    size_t asked = 0;
    d.set_agents_per_node_source([&]() { ++asked; return on_node; });
    d.process_once();
    EQ(d.stats().history.back().byte_budget, 15*GiB/6);
    on_node = 2;
    d.process_once();
    EQ(d.stats().history.back().byte_budget, 15*GiB/2);
    EQ(asked, 2);
    on_node = 0;
    d.process_once();
    EQ(d.stats().history.back().byte_budget, 15*GiB);
    // An explicit count wins over the source
    p.agents_per_node = 5;
    d.set_memory_policy(p);
    d.process_once();
    EQ(d.stats().history.back().byte_budget, 3*GiB);
    EQ(asked, 3);

    TEST_PASS
}

// A stop request (as ParDB uses for other agents' CLOSE_EARLY) is honored
// even without a memory policy of our own, but never before a filter has
// run in the pass: entries the filter does not run on (the "B"s, which the
// scan meets in hash order, often before any "A") must not end a pass that
// has done nothing
TEST(stop_requested_after_a_run) {
    g_TEST_max_filters_runs = 0;
    SeqDB d;
    MemoryPolicy p = MemoryPolicy::disabled();
    p.check_interval = chrono::milliseconds(0);
    d.set_memory_policy(p);
    EQ(d.memory_policy().enabled(), false);

    size_t runs_at_pass_start = 0, calls = 0;
    bool called_before_a_run = false;
    d.set_stop_requested_callback([&]() {
        ++calls;
        if (g_TEST_max_filters_runs == runs_at_pass_start) called_before_a_run = true;
        return true;
    });
    add_a_entries(d, 10);
    for (int k = 0; k < 10; ++k) { DBEntry<> e; e.clear().add_tag("B"); d.add_entry(e); }
    d.install_filter(make_shared<Filter>(&g_TEST_a_done_fi));

    vector<size_t> runs;
    while (true) {
        runs_at_pass_start = g_TEST_max_filters_runs;
        if (!d.process_once()) break;
        runs.push_back(g_TEST_max_filters_runs);
        if (runs.size() > 30) TEST_FAIL
    }
    // Every pass that ran stopped right after its one run
    NOPRINT_EQ(runs, (vector<size_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
    EQ(count_a_done(d), 10);
    EQ(called_before_a_run, false);
    // Once per pass that ran; the last pass ran nothing and never asked
    EQ(calls, 10);

    auto& h = d.stats().history;
    EQ(h.size(), 11);
    for (size_t i = 0; i < 10; ++i) {
        EQ(h[i].stop_reason, STOP_REMOTE);
        EQ(h[i].byte_budget, NO_BYTE_BUDGET);
    }
    EQ(h[10].stop_reason, STOP_NONE);

    TEST_PASS
}

// Every kind of filter output counts against the budget at its serialized
// size, and only output made by a running filter counts
static const char* const g_TEST_all_outputs_tags[] = {"out", "more", ""};
bool g_TEST_all_outputs_should_run(const DBAccess* acc) {
    return g_TEST_has_tag(acc, "src") && !g_TEST_has_tag(acc, "all:done");
}
void g_TEST_all_outputs_run(void* access) {
    const DBAccess* acc = (const DBAccess*)access;
    acc->make_new_entry.run(&acc->make_new_entry, g_TEST_all_outputs_tags, "hello", INITIAL_KEY);
    acc->add_tag.run(&acc->add_tag, "all:done");
    acc->remove_tag.run(&acc->remove_tag, acc->key, "gone");
    acc->update_entry_val.run(&acc->update_entry_val, acc->key, "new value");
    acc->subscribe_to_entry.run(&acc->subscribe_to_entry, acc->key, dbkey_t{9, 9, 9}, "waiting");
}
TEST(mem_bytes_count_each_output) {
    FilterInterface all_outputs {
        filter_name: "TEST_all_outputs",
        filter_type: SINGLE_ENTRY,
        should_run: &g_TEST_all_outputs_should_run,
        init: nullptr,
        destroy: nullptr,
        run: &g_TEST_all_outputs_run
    };
    SeqDB d;
    d.set_memory_policy(MemoryPolicy::disabled());
    { DBEntry<> e; e.clear().add_tag("src").add_tag("gone").value() = "v"; e.set_key({5, 5, 5}); d.add_entry(move(e)); }
    d.install_filter(make_shared<Filter>(&all_outputs));

    EQ(d.process_once(), true);

    size_t k = sizeof(dbkey_t);
    DBEntry<> new_entry { g_TEST_all_outputs_tags, "hello", INITIAL_KEY };
    size_t expected = new_entry.serialize_size()
        + k + strlen("all:done")            // add tag
        + k + strlen("gone")                // remove tag
        + k + strlen("new value")           // update value
        + 2*k + strlen("waiting");          // subscribe
    EQ(d.stats().history[0].bytes_emitted, expected);
    EQ(d.stats().bytes_emitted, expected);

    // Outside a filter run nothing is counted (the count is only reset when
    // the next pass starts, so this would show)
    d.note_bytes_out(100);
    EQ(d.stats().bytes_emitted, expected);

    // The outputs were applied as usual
    auto entry = d.entries()[dbkey_t{5, 5, 5}];
    EQ(entry.has_tag("all:done"), true);
    EQ(entry.has_tag("gone"), false);
    EQ(entry.value(), "new value");
    EQ(d.size(), 2);

    TEST_PASS
}

// Stopping early changes how the work is split into passes, never the
// result: a chain of filters (one tags entries for the other, possibly
// behind the cursor) gives the same DB with a tiny budget as with none
TEST(mem_budget_same_result) {
    FilterInterface tag_other {
        filter_name: "TEST_tag_other",
        filter_type: SINGLE_ENTRY,
        should_run: &g_TEST_tag_other_should_run,
        init: nullptr,
        destroy: nullptr,
        run: &g_TEST_tag_other_run
    };
    FilterInterface on_tag_b {
        filter_name: "TEST_on_tag_b",
        filter_type: SINGLE_ENTRY,
        should_run: &g_TEST_on_tag_b_should_run,
        init: nullptr,
        destroy: nullptr,
        run: &g_TEST_on_tag_b_run
    };
    auto build = [&](SeqDB& d) {
        for (size_t k = 0; k < 20; ++k) {
            { DBEntry<> e; e.clear().add_tag("X").value() = "x"; e.set_key({21, (vtx_t)k, 0}); d.add_entry(move(e)); }
            { DBEntry<> e; e.clear().add_tag("Y").value() = "y"; e.set_key({20, (vtx_t)k, 0}); d.add_entry(move(e)); }
        }
        d.install_filter(make_shared<Filter>(&tag_other));
        d.install_filter(make_shared<Filter>(&on_tag_b));
    };

    SeqDB unlimited;
    unlimited.set_memory_policy(MemoryPolicy::disabled());
    build(unlimited);
    g_TEST_on_tag_b_runs = 0;
    unlimited.process();
    size_t unlimited_runs = g_TEST_on_tag_b_runs;

    SeqDB limited;
    MemoryPolicy p = MemoryPolicy::disabled();
    p.pass_budget_bytes = 1;
    limited.set_memory_policy(p);
    build(limited);
    g_TEST_on_tag_b_runs = 0;
    size_t passes = 0;
    while (limited.process_once()) {
        if (++passes > 500) TEST_FAIL
    }
    EQ(g_TEST_on_tag_b_runs, unlimited_runs);
    EQ(g_TEST_on_tag_b_runs, 20);
    // It really did take more passes: one filter run per pass
    EQ(passes >= 40, true);

    auto a = unlimited.entries();
    auto b = limited.entries();
    EQ(a.size(), b.size());
    for (auto & [key, entry] : a) {
        EQ(b.count(key), 1);
        EQ(b[key] == entry, true);
    }

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
    RUN_TEST(max_filters_per_stage)
    RUN_TEST(max_filters_per_stage_rejects_zero)
    RUN_TEST(max_filters_per_stage_default_unlimited)
    RUN_TEST(cursor_runs_entries_behind_cursor)
    RUN_TEST(cursor_scan_cost)
    RUN_TEST(memory_policy_on_by_default)
    RUN_TEST(memory_policy_disabled)
    RUN_TEST(memory_policy_rejects_invalid)
    RUN_TEST(mem_budget_stops_pass)
    RUN_TEST(mem_high_at_start_still_progresses)
    RUN_TEST(mem_pressure_stops_pass)
    RUN_TEST(mem_budget_from_headroom)
    RUN_TEST(stop_requested_after_a_run)
    RUN_TEST(mem_bytes_count_each_output)
    RUN_TEST(mem_budget_same_result)

    elga::ZMQChatterbox::Teardown();
TESTS_END
