#include "test.hpp"
#include "par_db.hpp"
#include "test_helpers.hpp"

using namespace std;
using namespace pando;

localnum_t g_idx = 0;
size_t inc_amount = 3;


TEST(filter_new_entry) {
    g_idx = 0;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    // Install the filters on both
    c1.add_filter_dir(build_dir+"/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("BTC_block_to_tx");

    // Now, add in multiple entries
    dbkey_t key {1,2,4};
    DBEntry<> e; e.set_key(key);
    e.add_tag("BTC").add_tag("block").add_to_value("{\"tx\":[");
    size_t num_txs = 1000;
    for (size_t idx = 0; idx < num_txs; ++idx)
        e.add_to_value("{\"txid\":\""+to_string(idx)+"\"},");
    e.add_to_value("{\"txid\":\"" + to_string(num_txs) + "\"}]}");

    EQ(c1.db_size(), 0);
    EQ(c2.db_size(), 0);

    c1.add_entry(e);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.db_size(), 1);
    EQ(c2.db_size(), 0);

    // Now, c2 has one entry, c1 has none
    // Process the filters
    c1.process();
    c2.process();
    wait(addrs);

    size_t d1_s = c1.db_size();
    size_t d2_s = c2.db_size();

    EQ(d1_s+d2_s, num_txs+2);
    EQ(d1_s>0, true);
    EQ(d2_s>0, true);

    TEST_PASS
}

TEST(filter_new_entry_with_key) {
    g_idx = 1;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    // Install the filters on both
    c1.add_filter_dir(build_dir+"/test/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("test_pardb_filters_new_entry");

    // Now, add in the entry
    dbkey_t key {1,2,3};
    DBEntry<> e; e.set_key(key);
    e.add_tag("A").add_tag("tag1").add_to_value("test1");
    c1.add_entry(e);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.db_size(), 0);
    EQ(c2.db_size(), 1);

    // Now, c2 has one entry, c1 has none
    // Process the filters
    c1.process();
    wait(addrs);

    size_t d1_s = c1.db_size();
    size_t d2_s = c2.db_size();

    EQ(d1_s+d2_s, 2);
    EQ(d1_s>0, true);
    EQ(d2_s>0, true);

    vector<DBEntry<>> entries;
    auto c1_entries = c1.get_entries();
    auto c2_entries = c2.get_entries();
    move(c1_entries.begin(), c1_entries.end(), std::back_inserter(entries));
    move(c2_entries.begin(), c2_entries.end(), std::back_inserter(entries));

    dbkey_t new_key {2,2,2};
    bool new_entry_found = false;
    for (auto & entry : entries) {
        if (entry.get_key() == new_key) {
            EQ(new_entry_found, false)
            new_entry_found = true;
        }
    }

    EQ(new_entry_found, true);

    TEST_PASS
}

TEST(filter_fail) {
    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDB db2 { db2_addr };

    ParDBClient c1 { db1_addr };

    c1.add_neighbor(db2_addr);

    db2.recv_msg();
    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(db2.num_neighbors(), 2);

    // Install the filters on both
    c1.add_filter_dir(build_dir+"/filters");
    db2.add_filter_dir(build_dir+"/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("BTC_block_to_tx");
    db2.install_filter("not_a_valid_filter");

    TEST_PASS
}


TEST(dup_filters_fail) {
    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDB db1 { db1_addr };

    db1.add_filter_dir(build_dir+"/filters");
    db1.add_filter_dir(build_dir+"/filters");

    TEST_PASS
}

TEST(two_filters) {
    g_idx = 0;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    // Install the filters on both
    c1.add_filter_dir(build_dir+"/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("BTC_block_to_tx");
    c1.install_filter("BTC_tx_vals");

    // Now, add in multiple entries
    dbkey_t key {1,2,4};
    DBEntry<> e; e.set_key(key);
    e.add_tag("BTC").add_tag("block").add_to_value("{\"tx\":[");
    for (size_t idx = 0; idx < 1000; ++idx)
        e.add_to_value("{\"txid\":\""+to_string(idx)+"\"},");
    e.add_to_value("{\"txid\":\"1000\"}]}");

    EQ(c1.db_size(), 0);
    EQ(c2.db_size(), 0);

    c1.add_entry(e);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.db_size(), 1);
    EQ(c2.db_size(), 0);

    // Now, c2 has one entry, c1 has none
    // Process the filters
    c1.process();
    wait(addrs);

    size_t d1_s = c1.db_size();
    size_t d2_s = c2.db_size();

    EQ(d1_s+d2_s, 1002);
    EQ(d1_s>0, true);
    EQ(d2_s>0, true);

    TEST_PASS
}

TEST(installed_filters) {
    elga::ZMQAddress db_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db { db_addr };

    ParDBClient c { db_addr };

    c.add_filter_dir(build_dir+"/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c.install_filter("BTC_block_to_tx");
    this_thread::sleep_for(chrono::milliseconds(50));
    c.install_filter("BTC_tx_vals");
    this_thread::sleep_for(chrono::milliseconds(50));

    vector<string> filters = c.installed_filters();
    EQ(filters.size(), 2);
    bool block_to_tx_found = false;
    bool tx_vals_found = false;
    for (auto & f : filters) {
        if (f == "BTC_block_to_tx") {
            EQ(block_to_tx_found, false)
            block_to_tx_found = true;
        }
        else if (f == "BTC_tx_vals") {
            EQ(tx_vals_found, false)
            tx_vals_found = true;
        } else {
            TEST_FAIL
        }
    }

    EQ(block_to_tx_found, true)
    EQ(tx_vals_found, true)

    TEST_PASS
}

TEST(filter_update_val) {
    g_idx = 1;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    DBEntry<> e1;
    e1.clear().add_tag("A");
    e1.value() = "test1";
    dbkey_t key1 {2,2,4};
    e1.set_key(key1);
    c1.add_entry(e1);

    DBEntry<> e2;
    e2.clear().add_tag("B");
    e2.value() = "test2";
    dbkey_t key2 {2,3,4};
    e2.set_key(key2);
    c1.add_entry(e2);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.db_size(), 1);
    EQ(c2.db_size(), 1);

    c1.add_filter_dir(build_dir+"/test/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("test_pardb_filters_update_val");
    this_thread::sleep_for(chrono::milliseconds(50));

    c1.process();
    wait(addrs);

    bool entry_found = false;
    for (auto &entry : c2.get_entries()) {
        if (entry.value() == "NEW VALUE") {
            EQ(entry_found, false)
            entry_found = true;
        } else {
            TEST_FAIL
        }
    }
    EQ(entry_found, true)

    TEST_PASS
}
TEST(filter_get_entry_two_db) {
    g_idx = 7;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    DBEntry<> e1;
    e1.add_tag("A");
    e1.value() = "test1";
    dbkey_t key1 {2,2,4};
    e1.set_key(key1);
    c1.add_entry(e1);

    this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.db_size(), 1);

    DBEntry<> e2;
    e2.add_tag("B");
    e2.value() = "test2";
    dbkey_t key2 {2,4,4};
    e2.set_key(key2);
    c1.add_entry(e2);

    this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c2.db_size(), 1);

    c1.add_filter_dir(build_dir+"/test/filters");
    c1.install_filter("test_pardb_filters_get_entry");
    c1.process();
    wait(addrs);

    bool a_entry_found = false;
    bool b_entry_found = false;
    for (auto &entry : c1.get_entries()) {
        if (entry.has_tag("test_pardb_filters_get_entry:done")) {
            EQ(a_entry_found, false)
            a_entry_found = true;
        } else {
            TEST_FAIL
        }
    }
    for (auto &entry : c2.get_entries()) {
        if (entry.has_tag("B")) {
            EQ(b_entry_found, false)
            b_entry_found = true;
        } else {
            TEST_FAIL
        }
    }
    EQ(a_entry_found, true);
    EQ(b_entry_found, true);

    TEST_PASS
}

TEST(filter_get_entry_two_db_not_found) {
    g_idx = 7;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    DBEntry<> e1;
    e1.clear().add_tag("A");
    e1.value() = "test1";
    dbkey_t key1 {2,2,4};
    e1.set_key(key1);
    c1.add_entry(e1);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.db_size(), 1);
    EQ(c2.db_size(), 0);

    c1.add_filter_dir(build_dir+"/test/filters");
    c1.install_filter("test_pardb_filters_get_entry");
    c1.process();
    wait(addrs);

    EQ(c1.db_size(), 1);
    EQ(c2.db_size(), 0);

    bool fail_tag_found = false;
    for (auto &entry : c1.get_entries()) {
        if (entry.has_tag("test_pardb_filters_get_entry:fail")) {
            EQ(fail_tag_found, false)
            EQ(entry.has_tag("NOT_FOUND"), true);
            fail_tag_found = true;
        } else {
            TEST_FAIL
        }
    }

    EQ(fail_tag_found, true);

    TEST_PASS
}

TEST(filter_get_entry) {
    g_idx = 10;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };
    ParDBClient c1 { db1_addr };
    vector<ZMQAddress> addrs; addrs.push_back(db1_addr);


    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 1);

    DBEntry<> e1;
    e1.clear().add_tag("A");
    e1.value() = "test1";
    dbkey_t key1 {2,2,4};
    e1.set_key(key1);
    c1.add_entry(e1);

    DBEntry<> e2;
    e2.clear().add_tag("B");
    e2.value() = "test2";
    dbkey_t key2 {2,4,4};
    e2.set_key(key2);
    c1.add_entry(e2);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.db_size(), 2);

    c1.add_filter_dir(build_dir+"/test/filters");
    c1.install_filter("test_pardb_filters_get_entry");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.process();
    wait(addrs);

    EQ(c1.db_size(), 2);

    bool a_entry_found = false;
    bool b_entry_found = false;
    for (auto &entry : c1.get_entries()) {
        if (entry.has_tag("test_pardb_filters_get_entry:done")) {
            EQ(a_entry_found, false)
            a_entry_found = true;
        } else if (entry.has_tag("B")) {
            EQ(b_entry_found, false)
            b_entry_found = true;
        } else {
            TEST_FAIL
        }
    }
    EQ(a_entry_found, true);
    EQ(b_entry_found, true);

    TEST_PASS
}

TEST(filter_subscribe_two_db) {
    g_idx = 7;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    DBEntry<> e1;
    e1.clear().add_tag("A");
    e1.value() = "test1";
    dbkey_t key1 {2,2,4};
    e1.set_key(key1);
    c1.add_entry(e1);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.db_size(), 1);
    EQ(c2.db_size(), 0);

    c1.add_filter_dir(build_dir+"/test/filters");
    c1.install_filter("test_pardb_filters_subscribe");

    //Run the filter and make it subscribe because the entry it's looking for does not exist
    c1.process();
    wait(addrs);

    //Ensure the db state is correct
    EQ(c1.db_size(), 1);
    EQ(c2.db_size(), 0);
    bool wait_tag_found = false;
    for (auto &entry : c1.get_entries()) {
        if (entry.has_tag("wait"))
            wait_tag_found = true;
    }
    EQ(wait_tag_found, true);

    //Add the entry that it subscribed to
    DBEntry<> e2;
    e2.clear().add_tag("B");
    e2.value() = "test2";
    dbkey_t key2 {2,4,4};
    e2.set_key(key2);
    c1.add_entry(e2);

    this_thread::sleep_for(chrono::milliseconds(50));

    // process once after adding the entry to ensure the subscriptions are
    // finished properly
    c1.process(); wait(addrs);

    //And process again
    c1.process();
    wait(addrs);

    EQ(c1.db_size(), 1);
    EQ(c2.db_size(), 1);

    bool a_entry_found = false;
    bool b_entry_found = false;
    for (auto &entry : c1.get_entries()) {
        EQ(entry.has_tag("wait"), false);
        if (entry.has_tag("test_pardb_filters_subscribe:done")) {
            EQ(a_entry_found, false)
            a_entry_found = true;
        } else {
            TEST_FAIL
        }
    }
    for (auto &entry : c2.get_entries()) {
        EQ(entry.has_tag("wait"), false);
        if (entry.has_tag("B")) {
            EQ(b_entry_found, false)
            b_entry_found = true;
        } else {
            TEST_FAIL
        }
    }
    EQ(a_entry_found, true);
    EQ(b_entry_found, true);

    TEST_PASS
}

TEST(filter_remove_tag_two_db) {
    g_idx = 7;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };
    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    DBEntry<> e1;
    e1.clear().add_tag("A");
    e1.value() = "test1";
    dbkey_t key1 {2,2,4};
    e1.set_key(key1);
    c1.add_entry(e1);

    DBEntry<> e2;
    e2.clear().add_tag("B", "to_remove");
    e2.value() = "test2";
    dbkey_t key2 {2,4,4};
    e2.set_key(key2);
    c1.add_entry(e2);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.db_size(), 1);
    EQ(c2.db_size(), 1);

    c1.add_filter_dir(build_dir+"/test/filters");
    c1.install_filter("test_pardb_filters_remove_tag");

    c1.process();
    wait(addrs);
    this_thread::sleep_for(chrono::milliseconds(100));


    bool a_entry_found = false;
    bool b_entry_found = false;
    for (auto & entry : c1.get_entries()) {
        if (entry.has_tag("A")) {
            EQ(a_entry_found, false);
            a_entry_found = true;
        } else {
            TEST_FAIL
        }
    }
    for (auto & entry : c2.get_entries()) {
        if (entry.has_tag("B")) {
            EQ(b_entry_found, false);
            EQ(entry.has_tag("to_remove"), false);
            b_entry_found = true;
        } else {
            TEST_FAIL
        }
    }

    EQ(a_entry_found, true);
    EQ(b_entry_found, true);

    TEST_PASS
}

TEST(tx_vals_process_loop) {
    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    ParDBClient c1 { db1_addr };
    vector<ZMQAddress> addrs; addrs.push_back(db1_addr);

    // Install the filters
    c1.add_filter_dir(build_dir+"/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("BTC_block_to_tx");
    c1.install_filter("BTC_block_to_txtime");
    c1.install_filter("BTC_tx_vals");
    c1.install_filter("exchange_rates");
    c1.add_db_file(data_dir + "/simple_bitcoin.txt");
    c1.add_db_file(data_dir + "/exchange_rates/bitcoin_exchange_rate");

    this_thread::sleep_for(chrono::milliseconds(1000));

    c1.process();
    wait(addrs);

    size_t first_db_size = c1.db_size();

    c1.process();
    wait(addrs);

    //Make sure the second process didn't change the DB at all
    EQ(c1.db_size(), first_db_size);

    TEST_PASS
}

TEST(in_out_edges_bad_order) {
    //Same as in_and_out_edges, but this time we run the filters all together so that order is not guaranteed
    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    // Create the transaction JSON
    {
        DBEntry<> e; e.clear().add_tag("BTC", "tx").value() = "{\"tx\":{\"txid\": \"A\", \"vout\": [{\"value\": 10.0, \"n\": 0}, {\"value\": 9.0, \"n\": 1}, {\"value\": 5.0, \"n\": 2}, {\"value\": 1.2345,\"n\": 3}], \"vin\": [{\"coinbase\": \"12345\" }]}}";
        dbkey_t key {10000,0,0}; // value doesn't matter
        e.set_key(key);
        c1.add_entry(e);
    }
    {
        DBEntry<> e; e.clear().add_tag("BTC", "tx").value() = "{\"tx\":{\"txid\": \"B\", \"vout\": [{\"value\": 5.0, \"n\": 0}], \"vin\": [{\"txid\": \"A\", \"vout\": 0}]}}";
        dbkey_t key {10000,0,1}; // value doesn't matter
        e.set_key(key);
        c1.add_entry(e);
    }
    {
        DBEntry<> e; e.clear().add_tag("BTC", "tx").value() = "{\"tx\":{\"txid\": \"C\", \"vin\": [{\"txid\": \"A\", \"vout\": 1}, {\"txid\": \"B\", \"vout\": 0}], \"vout\": []}}";
        dbkey_t key {10000,0,2}; // value doesn't matter
        e.set_key(key);
        c1.add_entry(e);
    }

    c1.add_filter_dir(build_dir + "/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("BTC_tx_out_edges");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.process(); //force them to run out of order
    wait(addrs);
    c1.install_filter("BTC_tx_in_edges");
    c1.process();
    wait(addrs);

    bool e_01_in = false;
    bool e_12_in = false;
    bool e_02_in = false;
    bool e_01_out = false;
    bool e_12_out = false;
    bool e_02_out = false;
    bool e_03_out = false;
    bool e_04_out = false;

    vector<DBEntry<>> entries;
    auto c1_entries = c1.get_entries();
    auto c2_entries = c2.get_entries();
    move(c1_entries.begin(), c1_entries.end(), std::back_inserter(entries));
    move(c2_entries.begin(), c2_entries.end(), std::back_inserter(entries));

    for (auto & entry : entries) {
        if (entry.has_tag("tx-in-edge")) {
            if (entry.has_tag("from=A") && entry.has_tag("n=0") && entry.value() == "B")
                e_01_in = true;
            if (entry.has_tag("from=A") && entry.has_tag("n=1") && entry.value() == "C")
                e_12_in = true;
            if (entry.has_tag("from=B") && entry.has_tag("n=0") && entry.value() == "C")
                e_02_in = true;
        }
        if (entry.has_tag("tx-out-edge")) {
            if (entry.has_tag("from=A") && entry.has_tag("to=B")) {
                EQ(e_01_out, false);
                e_01_out = true;
                EQ(entry.value(), "10.000000");
            }
            else if (entry.has_tag("from=B") && entry.has_tag("to=C")) {
                EQ(e_12_out, false);
                e_12_out = true;
                EQ(entry.value(), "5.000000");
            }
            else if (entry.has_tag("from=A") && entry.has_tag("to=C")) {
                EQ(e_02_out, false);
                e_02_out = true;
                EQ(entry.value(), "9.000000");
            }
            else if (entry.has_tag("from=A") && entry.has_tag("to=UTXO") && entry.value() == "5.000000") {
                EQ(e_03_out, false);
                e_03_out = true;
            }
            else if (entry.has_tag("from=A") && entry.has_tag("to=UTXO") && entry.value() == "1.234500") {
                EQ(e_04_out, false);
                e_04_out = true;
            }
        }
    }

    EQ(e_01_in, true);
    EQ(e_12_in, true);
    EQ(e_02_in, true);

    EQ(e_01_out, true);
    EQ(e_12_out, true);
    EQ(e_02_out, true);
    EQ(e_03_out, true);
    EQ(e_04_out, true);


    TEST_PASS
}


TEST(subscribing_consensus) {
    vector<ZMQAddress> addrs;

    // Create the ParDBs
    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };
    addrs.push_back(db1_addr);

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };
    addrs.push_back(db2_addr);

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    c1.add_filter_dir(build_dir+"/test/filters");

    // The point of this test is to make sure that the following scenario
    // works: an entry subscribes to another entry. Then, that entry gets
    // created *by a filter*. We need to make sure this doesn't break the
    // "consensus" that the ParDBs come to with receiving the expected number
    // of messages

    // So, we first add the entry that will subscribe
    DBEntry<> e1;
    e1.clear().add_tag("A");
    e1.value() = "test1";
    dbkey_t key1 {1,1,1};
    e1.set_key(key1);
    c1.add_entry(e1);
    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.db_size() + c2.db_size(), 1);

    // Then, add the filter that causes it to subscribe. Nothing should happen,
    // but there should now be a subscription waiting
    c1.install_filter("test_pardb_filters_subscribe_and_create");
    c1.process(); wait(addrs);
    EQ(c1.db_size() + c2.db_size(), 1);

    // Now, install and run the filter that will cause the subscribee to be
    // created. This in turn will trigger the previous filter to run and create
    // a third entry
    c1.install_filter("test_pardb_filters_create");
    c1.process(); wait(addrs);
    this_thread::sleep_for(chrono::milliseconds(2000));

    vector<DBEntry<>> entries;
    auto c1_entries = c1.get_entries();
    auto c2_entries = c2.get_entries();
    move(c1_entries.begin(), c1_entries.end(), std::back_inserter(entries));
    move(c2_entries.begin(), c2_entries.end(), std::back_inserter(entries));

    EQ(c1.db_size() + c2.db_size(), 3);

    TEST_PASS
}

// A filter on one agent looks up keys owned by all three agents (and a
// missing one) with one batched call
TEST(filter_get_entries_by_keys) {
    g_idx = 0;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };
    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };
    elga::ZMQAddress db3_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db3 { db3_addr };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };
    ParDBClient c3 { db3_addr };

    vector<ZMQAddress> addrs { db1_addr, db2_addr, db3_addr };

    c2.add_neighbor(db1_addr);
    c3.add_neighbor(db1_addr);
    for (int t = 0; t < 100 && (c1.num_neighbors() != 3 || c2.num_neighbors() != 3 || c3.num_neighbors() != 3); ++t)
        this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.num_neighbors(), 3);
    EQ(c2.num_neighbors(), 3);
    EQ(c3.num_neighbors(), 3);

    c1.add_filter_dir(build_dir+"/test/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("test_pardb_filters_get_entries");

    // Values at {30, i, 0} = i, spread over the agents (placement hashes (a, b))
    size_t n = 2000;
    for (size_t i = 0; i < n; ++i) {
        DBEntry<> e; e.set_key({30, (vtx_t)i, 0});
        e.add_tag("value"); e.value() = to_string(i);
        c1.add_entry(e);
    }
    { DBEntry<> e; e.set_key({30, 5000, 0}); e.add_tag("value"); c1.add_entry(e); }   // empty value
    { DBEntry<> e; e.set_key({32, 0, 0}); e.add_tag("Q"); e.value() = to_string(n); c1.add_entry(e); }

    // Wait for forwarded entries to reach their owners
    for (int t = 0; t < 100 && c1.db_size() + c2.db_size() + c3.db_size() != n + 2; ++t)
        this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.db_size() + c2.db_size() + c3.db_size(), n + 2);
    // The keys really are spread over all three agents
    EQ(c1.db_size() > 0 && c2.db_size() > 0 && c3.db_size() > 0, true);

    c1.process();
    wait(addrs);

    string summary;
    for (auto* c : {&c1, &c2, &c3})
        for (auto & entry : c->get_entries())
            if (entry.get_key() == dbkey_t{33, 0, 0}) summary = entry.value();
    EQ(summary, "ok=" + to_string(n) + "/" + to_string(n) + " missing=1 dup=1 empty=1");

    TEST_PASS
}

TESTS_BEGIN
    elga::ZMQChatterbox::Setup();

    RUN_TEST(filter_new_entry)
    RUN_TEST(filter_new_entry_with_key)
    RUN_TEST(filter_get_entries_by_keys)
    RUN_TEST(filter_fail)
    RUN_TEST(dup_filters_fail)
    RUN_TEST(two_filters)
    RUN_TEST(installed_filters)
    RUN_TEST(tx_vals_process_loop)

    RUN_TEST(filter_update_val)
    RUN_TEST(filter_get_entry)
    RUN_TEST(filter_get_entry_two_db)
    RUN_TEST(filter_get_entry_two_db_not_found)
    RUN_TEST(filter_subscribe_two_db)
    RUN_TEST(filter_remove_tag_two_db)

    RUN_TEST(in_out_edges_bad_order)
    RUN_TEST(subscribing_consensus)

    elga::ZMQChatterbox::Teardown();
TESTS_END
