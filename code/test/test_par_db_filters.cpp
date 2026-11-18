#include "test.hpp"
#include "par_db.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <map>
#include <sstream>

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

// With max_filters_per_stage = 1, every agent closes its stage after each
// filter run. Processing must still loop (across agents) until every entry
// has been filtered.
TEST(filter_max_filters_per_stage) {
    g_idx = 0;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr, 2ull*(1ull<<29), false, 1 };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr, 2ull*(1ull<<29), false, 1 };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    c1.add_filter_dir(build_dir+"/test/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("test_pardb_filters_new_entry");

    // Entries the filter runs on (tag "A"), spread over both agents by key
    size_t num_a = 10;
    for (size_t k = 0; k < num_a; ++k) {
        DBEntry<> e; e.set_key({1, (vtx_t)(10+k), 0});  // placement hashes (a, b)
        e.add_tag("A").add_to_value("v"+to_string(k));
        c1.add_entry(e);
    }

    this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.db_size() + c2.db_size(), num_a);

    c1.process();
    wait(addrs);

    // Every "A" entry was filtered, and the filter's output entry {2,2,2}
    // (identical every time, so stored once) exists
    size_t done = 0;
    bool new_entry_found = false;
    vector<DBEntry<>> entries = c1.get_entries();
    auto c2_entries = c2.get_entries();
    move(c2_entries.begin(), c2_entries.end(), std::back_inserter(entries));
    for (auto & entry : entries) {
        if (entry.has_tag("A") && entry.has_tag("test_pardb_filters_new_entry:done")) ++done;
        if (entry.get_key() == dbkey_t{2,2,2}) new_entry_found = true;
    }
    EQ(done, num_a);
    EQ(new_entry_found, true);
    EQ(c1.db_size() + c2.db_size(), num_a+1);

    TEST_PASS
}

// A filter on one agent makes another filter runnable on entries owned by
// another agent, possibly behind that agent's scan cursor. With
// max_filters_per_stage = 1, every such entry must still be filtered.
TEST(filter_cursor_cross_agent) {
    g_idx = 0;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr, 2ull*(1ull<<29), false, 1 };

    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr, 2ull*(1ull<<29), false, 1 };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    vector<ZMQAddress> addrs; addrs.push_back(db1_addr); addrs.push_back(db2_addr);

    c1.add_neighbor(db2_addr);

    this_thread::sleep_for(chrono::milliseconds(50));

    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    c1.add_filter_dir(build_dir+"/test/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("test_pardb_filters_tag_other");
    c1.install_filter("test_pardb_filters_on_tag_b");

    // Y_i at {20, i, 0} tags X_i at {21, i, 0}; placement hashes (a, b), so
    // pairs are spread over the agents and often split between them
    size_t pairs = 10;
    for (size_t k = 0; k < pairs; ++k) {
        DBEntry<> x; x.set_key({21, (vtx_t)k, 0}); x.add_tag("X").add_to_value("x");
        c1.add_entry(x);
        DBEntry<> y; y.set_key({20, (vtx_t)k, 0}); y.add_tag("Y").add_to_value("y");
        c1.add_entry(y);
    }

    this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.db_size() + c2.db_size(), 2*pairs);

    c1.process();
    wait(addrs);

    size_t x_done = 0, y_done = 0;
    vector<DBEntry<>> entries = c1.get_entries();
    auto c2_entries = c2.get_entries();
    move(c2_entries.begin(), c2_entries.end(), std::back_inserter(entries));
    for (auto & entry : entries) {
        if (entry.has_tag("X") && entry.has_tag("test_pardb_filters_on_tag_b:done")) ++x_done;
        if (entry.has_tag("Y") && entry.has_tag("test_pardb_filters_tag_other:done")) ++y_done;
    }
    EQ(y_done, pairs);
    EQ(x_done, pairs);
    EQ(c1.db_size() + c2.db_size(), 2*pairs);

    TEST_PASS
}

// A processing request ends with exactly one pass that runs no filter, to
// check that nothing else became eligible. (The barrier before a pass's
// filters ran used to repeat the previous pass's answer, so every request
// ended with two such passes.) Every agent takes part in every pass.
TEST(process_request_ends_after_one_empty_pass) {
    g_idx = 0;
    const size_t n = 12;

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
    c1.install_filter("test_pardb_filters_new_entry");

    for (size_t k = 0; k < n; ++k) {
        DBEntry<> e; e.set_key({1, (vtx_t)(10+k), 0});
        e.add_tag("A").add_to_value("v"+to_string(k));
        c1.add_entry(e);
    }
    for (int t = 0; t < 100 && c1.db_size() + c2.db_size() + c3.db_size() != n; ++t)
        this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.db_size() + c2.db_size() + c3.db_size(), n);

    c1.process();
    wait(addrs);

    // The pass that runs the filters, then one that finds nothing to run
    EQ(db1.db().passes_started(), 2);
    EQ(db2.db().passes_started(), 2);
    EQ(db3.db().passes_started(), 2);

    size_t done = 0;
    for (auto* c : {&c1, &c2, &c3})
        for (auto & entry : c->get_entries())
            if (entry.has_tag("A") && entry.has_tag("test_pardb_filters_new_entry:done")) ++done;
    EQ(done, n);

    TEST_PASS
}

// An agent built from ParDBOptions uses its settings: with one filter run
// per pass, each "A" entry takes a pass of its own, and the scan reads the
// map two keys at a time
TEST(pardb_from_options) {
    g_idx = 0;
    const size_t n = 4;
    ParDBOptions opts;
    opts.max_filters_per_stage = 1;
    opts.mem_policy = MemoryPolicy::disabled();
    opts.scan_block_keys = 2;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr, 2ull*(1ull<<29), opts };
    EQ(db1.db().scan_block_keys(), 2);
    ParDBClient c1 { db1_addr };
    vector<ZMQAddress> addrs { db1_addr };
    this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.num_neighbors(), 1);

    c1.add_filter_dir(build_dir+"/test/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("test_pardb_filters_new_entry");
    for (size_t k = 0; k < n; ++k) {
        DBEntry<> e; e.set_key({1, (vtx_t)(10+k), 0});
        e.add_tag("A").add_to_value("v"+to_string(k));
        c1.add_entry(e);
    }
    EQ(c1.db_size(), n);

    c1.process();
    wait(addrs);

    EQ(db1.db().passes_started() >= n, true);
    size_t done = 0;
    for (auto & entry : c1.get_entries())
        if (entry.has_tag("A") && entry.has_tag("test_pardb_filters_new_entry:done")) ++done;
    EQ(done, n);

    TEST_PASS
}

// A filter on one agent looks up n keys owned by all three agents (and a
// missing one) with one batched call. The agent running the filter owns
// some of the keys itself, which it reads from its own PandoMap. With a
// small n, each owner's reply is only a few bytes (small enough for ZeroMQ
// to keep inside the message object). With a small max_keys_per_request,
// each owner's keys are split over several requests.
TEST(filter_get_entries_by_keys, size_t n, size_t max_keys_per_request) {
    g_idx = 0;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };
    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };
    elga::ZMQAddress db3_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db3 { db3_addr };

    if (max_keys_per_request != 0) {
        // Set on every agent: whichever owns the "Q" entry runs the filter
        db1.db().set_max_keys_per_request(max_keys_per_request);
        db2.db().set_max_keys_per_request(max_keys_per_request);
        db3.db().set_max_keys_per_request(max_keys_per_request);
    }

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
    // With many keys, they really are spread over all three agents
    if (n >= 100) EQ(c1.db_size() > 0 && c2.db_size() > 0 && c3.db_size() > 0, true);

    c1.process();
    wait(addrs);

    string summary;
    for (auto* c : {&c1, &c2, &c3})
        for (auto & entry : c->get_entries())
            if (entry.get_key() == dbkey_t{33, 0, 0}) summary = entry.value();
    EQ(summary, "ok=" + to_string(n) + "/" + to_string(n) + " missing=1 dup=1 empty=1");

    TEST_PASS
}

// get_entry_by_key gives the same results for keys the calling agent owns
// (read from its own PandoMap) as for keys another agent owns (through that
// agent's responder): values, empty values reported as missing, and keys
// that do not exist
TEST(filter_get_entry_by_key_local_and_remote) {
    g_idx = 0;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };
    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };

    vector<ZMQAddress> addrs { db1_addr, db2_addr };

    c1.add_neighbor(db2_addr);
    this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    c1.add_filter_dir(build_dir+"/test/filters");
    this_thread::sleep_for(chrono::milliseconds(50));
    c1.install_filter("test_pardb_filters_get_entry_many");

    size_t n = 200;
    for (size_t i = 0; i < n; ++i) {
        DBEntry<> e; e.set_key({34, (vtx_t)i, 0}); e.add_tag("value");
        if (i % 5 != 0) e.value() = "v" + to_string(i);    // every 5th value is empty
        c1.add_entry(e);
    }
    { DBEntry<> e; e.set_key({37, 0, 0}); e.add_tag("Q1"); e.value() = to_string(n); c1.add_entry(e); }

    for (int t = 0; t < 100 && c1.db_size() + c2.db_size() != n + 1; ++t)
        this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.db_size() + c2.db_size(), n + 1);
    // Both agents own some of the keys, so both paths are used
    EQ(c1.db_size() > 0 && c2.db_size() > 0, true);

    c1.process();
    wait(addrs);

    string summary;
    for (auto* c : {&c1, &c2})
        for (auto & entry : c->get_entries())
            if (entry.get_key() == dbkey_t{36, 0, 0}) summary = entry.value();
    EQ(summary, "ok=" + to_string(2*n) + "/" + to_string(2*n));

    TEST_PASS
}

// Run test_pardb_filters_mixed_writes on 3 agents with the given write
// batch flush size (0 = one message per write) and filter runs per pass
// (0 = default), check the result, and return every entry as
// "a:b:c" -> "sorted tags | value" (for {52,0,0}, whose value is concatenated
// in an arbitrary order, the value's lines are sorted)
static int run_mixed_writes(const string& build_dir, size_t batch_bytes, size_t max_filters,
                            map<string, string>& snapshot, uint64_t& batches, uint64_t& batched_writes) {
    g_idx = 0;
    const size_t n = 30;
    const size_t sz = 2ull*(1ull<<29);

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr, sz, false, max_filters, batch_bytes };
    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr, sz, false, max_filters, batch_bytes };
    elga::ZMQAddress db3_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db3 { db3_addr, sz, false, max_filters, batch_bytes };

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
    c1.install_filter("test_pardb_filters_mixed_writes");

    for (size_t i = 0; i < n; ++i) {
        { DBEntry<> e; e.set_key({50, (vtx_t)i, 0}); e.add_tag("W"); e.value() = to_string(i); c1.add_entry(e); }
        { DBEntry<> e; e.set_key({53, (vtx_t)i, 0}); e.add_tag("T"); e.add_tag("keep"); e.value() = "orig"; c1.add_entry(e); }
        { DBEntry<> e; e.set_key({51, (vtx_t)i, 1}); e.add_tag("waiting"); e.value() = "sub"; c1.add_entry(e); }
    }
    for (int t = 0; t < 100 && c1.db_size() + c2.db_size() + c3.db_size() != 3*n; ++t)
        this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.db_size() + c2.db_size() + c3.db_size(), 3*n);

    c1.process();
    wait(addrs);

    batches = db1.db().write_batches_sent() + db2.db().write_batches_sent() + db3.db().write_batches_sent();
    batched_writes = db1.db().writes_batched() + db2.db().writes_batched() + db3.db().writes_batched();

    vector<DBEntry<>> entries;
    for (auto* c : {&c1, &c2, &c3})
        for (auto & entry : c->get_entries())
            entries.push_back(move(entry));

    // The 3n preloaded entries, n created {51,i,0}, and the shared {52,0,0}
    EQ(entries.size(), 4*n + 1);

    snapshot.clear();
    size_t done = 0, made = 0, released = 0, updated = 0;
    for (auto & entry : entries) {
        dbkey_t k = entry.get_key();
        vector<string> tags(entry.tags().begin(), entry.tags().end());
        sort(tags.begin(), tags.end());
        string value = entry.value();
        if (k == dbkey_t{52, 0, 0}) {
            vector<string> lines;
            stringstream ss(value);
            for (string line; getline(ss, line);) lines.push_back(line);
            sort(lines.begin(), lines.end());
            EQ(lines.size(), n);
            value.clear();
            for (auto & line : lines) value += line + "\n";
            EQ(entry.has_tag("shared"), true);
        }
        string tag_list;
        for (auto & t : tags) tag_list += t + ",";
        snapshot[to_string(k.a) + ":" + to_string(k.b) + ":" + to_string(k.c)] = tag_list + "|" + value;

        string idx = to_string(k.b);
        if (k.a == 50) {
            EQ(entry.has_tag("test_pardb_filters_mixed_writes:done"), true);
            ++done;
        } else if (k.a == 51 && k.c == 0) {
            EQ(entry.has_tag("made"), true);
            EQ(value, "v" + idx);
            ++made;
        } else if (k.a == 51 && k.c == 1) {
            // Released by its subscription to {51,i,0}
            EQ(entry.has_tag("waiting"), false);
            ++released;
        } else if (k.a == 53) {
            EQ(entry.has_tag("T"), false);
            EQ(entry.has_tag("keep"), true);
            // The second of the two updates, in the order they were made
            EQ(value, "u" + idx);
            ++updated;
        }
    }
    EQ(done, n);
    EQ(made, n);
    EQ(released, n);
    EQ(updated, n);

    TEST_PASS
}

// Every kind of write a filter can make gives the same result batched as
// unbatched: unbatched (0), batches flushed after almost every write (64
// bytes, so each owner gets many batches per pass), and the default size
// (each owner gets one batch, at the end of the pass)
//
// The batch counters show batching really happened: each filter run makes 7
// writes (2 new entries, a tag removal, 2 value updates, a subscription and
// its own done tag), so 7n writes must go out in batches when batching is on
TEST(filter_write_batching) {
    const uint64_t writes = 7 * 30;
    map<string, string> unbatched, tiny, by_default;
    uint64_t batches, batched;

    EQ(run_mixed_writes(build_dir, 0, 0, unbatched, batches, batched), 0);
    EQ(batches, 0);
    EQ(batched, 0);

    // A 64-byte batch is flushed once it holds 64 bytes: no write is smaller
    // than 34 bytes, so a batch holds at most 2 writes
    EQ(run_mixed_writes(build_dir, 64, 0, tiny, batches, batched), 0);
    EQ(batched, writes);
    EQ(batches >= writes / 2, true);

    // At the default size nothing fills up: each of the 3 agents sends at
    // most one batch to each of the 3 owners, at the end of the one pass
    // that runs filters
    EQ(run_mixed_writes(build_dir, ParDB::DEFAULT_WRITE_BATCH_BYTES, 0, by_default, batches, batched), 0);
    EQ(batched, writes);
    EQ(batches >= 1 && batches <= 9, true);

    EQ(tiny == unbatched, true);
    EQ(by_default == unbatched, true);

    TEST_PASS
}

// With one filter run per pass, batches are flushed at every pass boundary;
// processing must still finish with the same result
TEST(filter_write_batching_one_filter_per_pass) {
    map<string, string> unbatched, batched;
    uint64_t batches, batched_writes;
    EQ(run_mixed_writes(build_dir, 0, 1, unbatched, batches, batched_writes), 0);
    EQ(batches, 0);
    EQ(run_mixed_writes(build_dir, 64, 1, batched, batches, batched_writes), 0);
    EQ(batched_writes, 7 * 30);
    EQ(batched == unbatched, true);

    TEST_PASS
}

// Run test_pardb_filters_edge_writes on 3 agents with the given write batch
// flush size, check every value, and return every entry as
// "a:b:c" -> "sorted tags | value"
static int run_edge_writes(const string& build_dir, size_t batch_bytes,
                           map<string, string>& snapshot, uint64_t& batches, uint64_t& batched_writes) {
    g_idx = 0;
    const size_t n = 6;
    const size_t sz = 2ull*(1ull<<29);
    const string odd_value = "a,b|c:d\n\x01\x7f\xff end";   // as in the filter
    auto big_value = [](vtx_t i) { return string(300*1024 + i, (char)('a' + i % 26)); };

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr, sz, false, 0, batch_bytes };
    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr, sz, false, 0, batch_bytes };
    elga::ZMQAddress db3_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db3 { db3_addr, sz, false, 0, batch_bytes };

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
    c1.install_filter("test_pardb_filters_edge_writes");

    for (size_t i = 0; i < n; ++i) {
        { DBEntry<> e; e.set_key({70, (vtx_t)i, 0}); e.add_tag("E"); e.value() = to_string(i); c1.add_entry(e); }
        { DBEntry<> e; e.set_key({73, (vtx_t)i, 0}); e.add_tag("to_empty"); e.value() = "orig"; c1.add_entry(e); }
        { DBEntry<> e; e.set_key({74, (vtx_t)i, 0}); e.add_tag("to_big"); e.value() = "orig"; c1.add_entry(e); }
    }
    for (int t = 0; t < 100 && c1.db_size() + c2.db_size() + c3.db_size() != 3*n; ++t)
        this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.db_size() + c2.db_size() + c3.db_size(), 3*n);

    c1.process();
    wait(addrs);

    batches = db1.db().write_batches_sent() + db2.db().write_batches_sent() + db3.db().write_batches_sent();
    batched_writes = db1.db().writes_batched() + db2.db().writes_batched() + db3.db().writes_batched();

    vector<DBEntry<>> entries;
    for (auto* c : {&c1, &c2, &c3})
        for (auto & entry : c->get_entries())
            entries.push_back(move(entry));
    EQ(entries.size(), 6*n);

    snapshot.clear();
    size_t checked = 0;
    for (auto & entry : entries) {
        dbkey_t k = entry.get_key();
        vector<string> tags(entry.tags().begin(), entry.tags().end());
        sort(tags.begin(), tags.end());
        string tag_list;
        for (auto & t : tags) tag_list += t + ",";
        snapshot[to_string(k.a) + ":" + to_string(k.b) + ":" + to_string(k.c)] = tag_list + "|" + entry.value();

        if (k.a == 70) { EQ(entry.has_tag("test_pardb_filters_edge_writes:done"), true); ++checked; }
        else if (k.a == 71) { EQ(entry.has_tag("big"), true); EQ(entry.value() == big_value(k.b), true); ++checked; }
        else if (k.a == 72) { EQ(entry.has_tag("empty"), true); EQ(entry.value().size(), 0); ++checked; }
        else if (k.a == 73) { EQ(entry.value().size(), 0); ++checked; }
        else if (k.a == 74) { EQ(entry.value() == string(200*1024, 'z'), true); ++checked; }
        else if (k.a == 75) { EQ(entry.has_tag("odd"), true); EQ(entry.value() == odd_value, true); ++checked; }
    }
    EQ(checked, 6*n);

    TEST_PASS
}

// Unusual values are carried exactly, batched or not: values larger than a
// whole batch (each of those forces its owner's batch out on its own), empty
// values, and values with separators, a newline, control and high bytes.
// Each run makes 6 writes (3 new entries, 2 value updates, its done tag).
TEST(filter_write_batching_edge_values) {
    const uint64_t n = 6, writes = 6 * n;
    map<string, string> unbatched, tiny, by_default;
    uint64_t batches, batched;

    EQ(run_edge_writes(build_dir, 0, unbatched, batches, batched), 0);
    EQ(batches, 0);

    EQ(run_edge_writes(build_dir, 64, tiny, batches, batched), 0);
    EQ(batched, writes);

    // Every 300 KB value is bigger than the 256 KB default, so its batch is
    // sent as soon as it is added: at least one batch per run
    EQ(run_edge_writes(build_dir, ParDB::DEFAULT_WRITE_BATCH_BYTES, by_default, batches, batched), 0);
    EQ(batched, writes);
    EQ(batches >= n, true);

    EQ(tiny == unbatched, true);
    EQ(by_default == unbatched, true);

    TEST_PASS
}

// Sends hand-built messages to one agent, as ParDBClient does
struct RawSender : ZMQChatterbox {
    ZMQRequester req_;
    RawSender(ZMQAddress server) : ZMQChatterbox(ZMQAddress {}), req_(server, addr_) { }
    void send_and_wait(const vector<char>& msg) {
        req_.send(msg.data(), msg.size());
        req_.wait_ack();
    }
};

static vector<char> key_string_msg(msg_type_t type, dbkey_t key, const string& str) {
    vector<char> msg(sizeof(msg_type_t) + sizeof(dbkey_t) + str.size());
    char* p = msg.data();
    pack_msg(p, type);
    pack_single(p, key);
    memcpy(p, str.data(), str.size());
    return msg;
}

// Before processing, a tag add, tag removal, value update or subscription
// that reaches an agent which does not own the key is forwarded to the owner
// whole (the forwarded message must start at the message type, not part way
// into the key)
TEST(forward_writes_to_owner) {
    g_idx = 0;

    elga::ZMQAddress db1_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db1 { db1_addr };
    elga::ZMQAddress db2_addr { "127.0.0.1", g_idx+=inc_amount };
    ParDBThread db2 { db2_addr };
    vector<ZMQAddress> addrs { db1_addr, db2_addr };

    ParDBClient c1 { db1_addr };
    ParDBClient c2 { db2_addr };
    c2.add_neighbor(db1_addr);
    for (int t = 0; t < 100 && (c1.num_neighbors() != 2 || c2.num_neighbors() != 2); ++t)
        this_thread::sleep_for(chrono::milliseconds(50));
    EQ(c1.num_neighbors(), 2);
    EQ(c2.num_neighbors(), 2);

    // Keys db1 owns (placement hashes (a, b), so all three share an owner)
    vtx_t b = 0;
    while (!db1.db().has_ownership(dbkey_t{60, b, 0})) ++b;
    dbkey_t target {60, b, 0};     // tags and value are changed
    dbkey_t waiter {60, b, 1};     // subscribes to the creation of created
    dbkey_t created {60, b, 2};
    EQ(db2.db().has_ownership(target), false);

    { DBEntry<> e; e.set_key(target); e.add_tag("T"); e.value() = "orig"; c1.add_entry(e); }
    { DBEntry<> e; e.set_key(waiter); e.add_tag("waiting"); e.value() = "sub"; c1.add_entry(e); }
    EQ(c1.db_size(), 2);

    // All sent to db2, which must forward them to db1, in this order
    RawSender to_db2 { db2_addr };
    to_db2.send_and_wait(key_string_msg(ADD_TAG_TO_ENTRY, target, "added"));
    {
        string tag = "waiting";
        vector<char> msg(sizeof(msg_type_t) + 2*sizeof(dbkey_t) + tag.size());
        char* p = msg.data();
        pack_msg(p, SUBSCRIBE_TO_ENTRY);
        pack_single(p, waiter);
        pack_single(p, created);
        memcpy(p, tag.data(), tag.size());
        to_db2.send_and_wait(msg);
    }
    to_db2.send_and_wait(key_string_msg(REMOVE_TAG_FROM_ENTRY, target, "T"));
    to_db2.send_and_wait(key_string_msg(UPDATE_ENTRY_VAL, target, "new"));

    // Tag removals and value updates are applied right away before
    // processing. Once the last one shows, db1 has handled the earlier ones.
    auto target_entry = [&]() {
        for (auto & e : c1.get_entries())
            if (e.get_key() == target) return e;
        return DBEntry<> {};
    };
    for (int t = 0; t < 100 && target_entry().value() != "new"; ++t)
        this_thread::sleep_for(chrono::milliseconds(50));
    EQ(target_entry().value(), "new");
    EQ(target_entry().has_tag("T"), false);

    // Creating the entry releases the subscription; the release and the tag
    // add are staged, and applied when a stage closes
    { DBEntry<> e; e.set_key(created); e.value() = "c"; c1.add_entry(e); }
    c1.process();
    wait(addrs);

    EQ(c1.db_size(), 3);
    EQ(c2.db_size(), 0);
    bool waiter_found = false;
    for (auto & e : c1.get_entries()) {
        if (e.get_key() == target) {
            EQ(e.has_tag("added"), true);
            EQ(e.has_tag("T"), false);
            EQ(e.value(), "new");
        } else if (e.get_key() == waiter) {
            EQ(e.has_tag("waiting"), false);
            waiter_found = true;
        }
    }
    EQ(waiter_found, true);

    TEST_PASS
}

TESTS_BEGIN
    elga::ZMQChatterbox::Setup();

    RUN_TEST(filter_new_entry)
    RUN_TEST(filter_new_entry_with_key)
    RUN_TEST(filter_max_filters_per_stage)
    RUN_TEST(filter_cursor_cross_agent)
    RUN_TEST(filter_write_batching)
    RUN_TEST(filter_write_batching_one_filter_per_pass)
    RUN_TEST(filter_write_batching_edge_values)
    RUN_TEST(forward_writes_to_owner)
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
    RUN_TEST(process_request_ends_after_one_empty_pass)
    RUN_TEST(pardb_from_options)
    RUN_TEST(filter_get_entries_by_keys, 2000, 0)
    RUN_TEST(filter_get_entries_by_keys, 2, 0)
    RUN_TEST(filter_get_entries_by_keys, 2000, 7)
    RUN_TEST(filter_get_entry_by_key_local_and_remote)

    elga::ZMQChatterbox::Teardown();
TESTS_END
