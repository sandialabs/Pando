#include <iostream>
#include <string>

#include "seq_db.hpp"

#include "test.hpp"

using namespace std;
using namespace pando;

TEST(add_tag) {
    SeqDB db;

    {DBEntry e; e.add_tag("A", "B"); e.value() = "test1"; db.add_entry(move(e));}
    {DBEntry e; e.add_tag("A", "C"); e.value() = "test2"; db.add_entry(move(e));}
    {DBEntry e; e.add_tag("B", "C"); e.value() = "test3"; db.add_entry(move(e));}

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_add_tag");

    for (auto & [key, entry] : db.entries())
        EQ(entry.has_tag("test_python_filter_add_tag:done"), false);


    EQ(db.size(), 3)

    db.process();

    for (auto & [key, entry] : db.entries()) {
        if (entry.has_tag("A")) {
            EQ(entry.has_tag("test_python_filter_add_tag:done"), true);
        } else {
            EQ(entry.has_tag("test_python_filter_add_tag:done"), false);
        }
    }
    EQ(db.size(), 3)

    TEST_PASS
}

TEST(remove_tag) {
    SeqDB db;

    {DBEntry e; e.add_tag("should_run", "A"); e.value() = "test1"; db.add_entry(move(e));}
    {DBEntry e; e.add_tag("should_run", "B"); e.value() = "test2"; db.add_entry(move(e));}
    {DBEntry e; e.add_tag("should_run", "C"); e.value() = "test3"; db.add_entry(move(e));}

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_remove_tag");

    for (auto & [key, entry] : db.entries())
        EQ(entry.has_tag("should_run"), true);


    EQ(db.size(), 3)

    db.process();

    for (auto & [key, entry] : db.entries()) {
        EQ(entry.has_tag("should_run"), false);
    }
    EQ(db.size(), 3)


    TEST_PASS
}

TEST(subscribe_to_entry) {
    SeqDB db;

    {
        DBEntry e; e.add_tag("A"); e.value() = "test1";
        dbkey_t key {1,1,1};
        e.set_key(key);
        db.add_entry(move(e));
    }

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_subscribe_to_entry");

    EQ(db.size(), 1)

    db.process();

    // After running once, the db size should not have changed, but its only entry should have the inactive tag
    EQ(db.size(), 1)
    for (auto & [key, entry] : db.entries()) {
        EQ(entry.has_tag("test_python_filter_subscribe_to_entry:inactive"), true)
    }

    // Add entry that was subscribed to
    {
        DBEntry e; e.add_tag("B"); e.value() = "test2";
        dbkey_t key {2,2,2};
        e.set_key(key);
        db.add_entry(move(e));
    }

    db.stage_close();

    EQ(db.size(), 2)

    db.process();

    EQ(db.size(), 2)

    bool done_tag_found = false;
    for (auto & [key, entry] : db.entries()) {
        if (entry.has_tag("test_python_filter_subscribe_to_entry:done")) {
            EQ(done_tag_found, false)
            done_tag_found = true;
        }
    }

    EQ(done_tag_found, true)
    

    TEST_PASS
}

TEST(get_entry_by_key) {
    SeqDB db;

    {
        DBEntry e; e.add_tag("A"); e.value() = "test1";
        dbkey_t key {1,1,1};
        e.set_key(key);
        db.add_entry(move(e));
    }
    {
        DBEntry e; e.add_tag("B"); e.value() = "test2";
        dbkey_t key {2,2,2};
        e.set_key(key);
        db.add_entry(move(e));
    }

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_get_entry_by_key");

    EQ(db.size(), 2)

    db.process();

    EQ(db.size(), 2)

    bool success = false;
    dbkey_t match_key {1,1,1};
    for (auto & [key, entry] : db.entries()) {
        if (key == match_key) {
            EQ(success, false);
            EQ(entry.has_tag("SUCCESS"), true)
            EQ(entry.has_tag("FAILURE"), false)
            success = true;
        }
    }

    EQ(success, true)
    

    TEST_PASS

}

TEST(get_entry_by_key_not_found) {
    SeqDB db;

    {
        DBEntry e; e.add_tag("A"); e.value() = "test1";
        dbkey_t key {1,1,1};
        e.set_key(key);
        db.add_entry(move(e));
    }

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_get_entry_by_key_not_found");

    EQ(db.size(), 1)

    db.process();

    EQ(db.size(), 1)

    bool success = false;
    dbkey_t match_key {1,1,1};
    for (auto & [key, entry] : db.entries()) {
        if (key == match_key) {
            EQ(success, false);
            EQ(entry.has_tag("SUCCESS"), true)
            EQ(entry.has_tag("FAILURE"), false)
            success = true;
        }
    }

    EQ(success, true)
    

    TEST_PASS
}

TEST(get_entries_by_keys) {
    SeqDB db;

    {
        DBEntry e; e.add_tag("A"); e.value() = "test1";
        dbkey_t key {1,1,1};
        e.set_key(key);
        db.add_entry(move(e));
    }
    {
        DBEntry e; e.add_tag("B"); e.value() = "test2";
        dbkey_t key {2,2,2};
        e.set_key(key);
        db.add_entry(move(e));
    }
    {
        DBEntry e; e.add_tag("B"); e.value() = "test3";
        dbkey_t key {3,3,3};
        e.set_key(key);
        db.add_entry(move(e));
    }
    {
        DBEntry e; e.add_tag("B");     // empty value
        dbkey_t key {4,4,4};
        e.set_key(key);
        db.add_entry(move(e));
    }

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_get_entries_by_keys");

    db.process();

    EQ(db.size(), 4)

    bool found = false;
    dbkey_t match_key {1,1,1};
    for (auto & [key, entry] : db.entries()) {
        if (key == match_key) {
            found = true;
            EQ(entry.has_tag("SUCCESS"), true)
            EQ(entry.has_tag("FAILURE"), false)
        }
    }
    EQ(found, true)

    TEST_PASS
}

TEST(make_new_entry) {
    SeqDB db;

    {
        DBEntry e; e.add_tag("A"); e.value() = "test1";
        dbkey_t key {1,1,1};
        e.set_key(key);
        db.add_entry(move(e));
    }

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_make_new_entry");

    EQ(db.size(), 1)

    db.process();

    EQ(db.size(), 2)

    TEST_PASS
}

TEST(update_entry_val_self) {
    SeqDB db;

    {
        DBEntry e; e.add_tag("A"); e.value() = "test1";
        dbkey_t key {1,1,1};
        e.set_key(key);
        db.add_entry(move(e));
    }

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_update_entry_val_self");

    EQ(db.size(), 1)

    db.process();

    EQ(db.size(), 1)

    for (auto & [key, entry] : db.entries()) {
        EQ(entry.value(), "my new value")
    }

    TEST_PASS
}

TEST(update_entry_val_other) {
    SeqDB db;

    {
        DBEntry e; e.add_tag("A"); e.value() = "test1";
        dbkey_t key {1,1,1};
        e.set_key(key);
        db.add_entry(move(e));
    }
    {
        DBEntry e; e.add_tag("B"); e.value() = "test2";
        dbkey_t key {2,2,2};
        e.set_key(key);
        db.add_entry(move(e));
    }

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_update_entry_val_other");

    EQ(db.size(), 2)

    db.process();

    EQ(db.size(), 2)

    bool updated_found = false;
    for (auto & [key, entry] : db.entries()) {
        if (entry.has_tag("B")) {
            EQ(updated_found, false)
            EQ(entry.value(), "my new value")
            updated_found = true;
        }
    }

    EQ(updated_found, true)

    TEST_PASS
}

TEST(import_libs) {
    SeqDB db;

    {
        DBEntry e; e.add_tag("A"); e.value() = "test1";
        dbkey_t key {1,1,1};
        e.set_key(key);
        db.add_entry(move(e));
    }
    {
        DBEntry e; e.add_tag("B"); e.value() = "test2";
        dbkey_t key {2,2,2};
        e.set_key(key);
        db.add_entry(move(e));
    }

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_import_libs");

    EQ(db.size(), 2)

    db.process();

    EQ(db.size(), 2)

    TEST_PASS
}

TEST(multiple_filters) {
    SeqDB db;

    {DBEntry e; e.add_tag("A", "B"); e.value() = "test1"; db.add_entry(move(e));}
    {DBEntry e; e.add_tag("A", "C"); e.value() = "test2"; db.add_entry(move(e));}
    {DBEntry e; e.add_tag("B", "C"); e.value() = "test3"; db.add_entry(move(e));}

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_add_tag");
    db.install_filter("test_python_filter_import_libs");

    for (auto & [key, entry] : db.entries())
        EQ(entry.has_tag("test_python_filter_add_tag:done"), false);


    EQ(db.size(), 3)

    db.process();

    for (auto & [key, entry] : db.entries()) {
        if (entry.has_tag("A")) {
            EQ(entry.has_tag("test_python_filter_add_tag:done"), true);
        } else {
            EQ(entry.has_tag("test_python_filter_add_tag:done"), false);
        }
    }
    EQ(db.size(), 3)


    TEST_PASS
}

TEST(pack_chain_info) {
    SeqDB db;

    {DBEntry e; e.add_tag("A"); e.value() = "test1"; db.add_entry(move(e));}

    db.add_filter_dir(build_dir + "/test/filters");
    db.install_filter("test_python_filter_pack_chain_info");

    EQ(db.size(), 1)

    db.process();

    chain_info_t ci = pack_chain_info(ZEC_KEY, TXTIME_KEY, 0);

    bool tag_found = false;
    for (auto & [key, entry] : db.entries()) {
        if (entry.has_tag(to_string(ci))) {
            tag_found = true;
        }
    }
    EQ(tag_found, true)

    TEST_PASS
}


TESTS_BEGIN
    elga::ZMQChatterbox::Setup();
    RUN_TEST(add_tag)
    RUN_TEST(remove_tag)
    RUN_TEST(subscribe_to_entry)
    RUN_TEST(get_entry_by_key)
    RUN_TEST(get_entry_by_key_not_found)
    RUN_TEST(get_entries_by_keys)
    RUN_TEST(make_new_entry)
    RUN_TEST(update_entry_val_self)
    RUN_TEST(update_entry_val_other)
    RUN_TEST(import_libs)
    RUN_TEST(multiple_filters)
    RUN_TEST(pack_chain_info)
    elga::ZMQChatterbox::Teardown();
    //Performance test TODO
TESTS_END
