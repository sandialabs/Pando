#include <iostream>
#include <sstream>
#include <string>

#include "seq_db.hpp"

#include "test.hpp"

using namespace std;
using namespace pando;

// Entries tagged MERGE_STRATEGY=SUM that share a key must have their values
// added, whether they meet in the staging area (filters' make_new_entry during
// process()) or when an entry lands on one already in the DB.

static DBEntry<> sum_entry(dbkey_t key, const string& value) {
    DBEntry<> e;
    e.add_tag("total").add_tag("MERGE_STRATEGY=SUM");
    // Set the value exactly, as make_new_entry does
    e.value() = value;
    e.set_key(key);
    return e;
}

/** @brief The entry stored at key, or an entry with INITIAL_KEY if missing */
static DBEntry<> entry_at(SeqDB& db, dbkey_t key) {
    auto entries = db.entries();
    auto it = entries.find(key);
    if (it == entries.end()) return DBEntry<> {};
    DBEntry<> e = it->second;
    e.set_key(key);
    return e;
}

TEST(identical_staged_entries_are_summed) {
    SeqDB db;
    dbkey_t key {1, 2, 3};

    // Identical "1" values used to compare equal and the second was dropped
    db.stage_add_entry(sum_entry(key, "1"));
    db.stage_add_entry(sum_entry(key, "1"));
    db.stage_add_entry(sum_entry(key, "1"));
    db.stage_close();

    EQ(db.size(), 1)
    auto e = entry_at(db, key);
    EQ(e.value(), "3")
    EQ(e.has_tag("MERGED_SUM"), true)

    TEST_PASS
}

TEST(different_staged_entries_are_summed) {
    SeqDB db;
    dbkey_t key {1, 2, 4};

    // Differing values used to be concatenated ("2\n5") instead of added
    db.stage_add_entry(sum_entry(key, "2"));
    db.stage_add_entry(sum_entry(key, "5"));
    db.stage_close();

    EQ(entry_at(db, key).value(), "7")

    TEST_PASS
}

TEST(staged_entry_sums_with_existing_entry) {
    SeqDB db;
    dbkey_t key {1, 2, 5};

    db.add_entry(sum_entry(key, "4"));
    db.stage_add_entry(sum_entry(key, "1"));
    db.stage_add_entry(sum_entry(key, "1"));
    db.stage_close();

    EQ(db.size(), 1)
    EQ(entry_at(db, key).value(), "6")

    TEST_PASS
}

TEST(separate_keys_stay_separate) {
    SeqDB db;
    dbkey_t k1 {1, 3, 0};
    dbkey_t k2 {1, 3, 1};

    db.stage_add_entry(sum_entry(k1, "1"));
    db.stage_add_entry(sum_entry(k2, "1"));
    db.stage_add_entry(sum_entry(k1, "1"));
    db.stage_close();

    EQ(db.size(), 2)
    EQ(entry_at(db, k1).value(), "2")
    EQ(entry_at(db, k2).value(), "1")

    TEST_PASS
}

TEST(add_entry_sums_integers_decimals_and_fails_on_text) {
    SeqDB db;
    dbkey_t k1 {2, 1, 1};
    dbkey_t k2 {2, 2, 2};
    dbkey_t k3 {2, 3, 3};

    db.add_entry(sum_entry(k1, "1"));
    db.add_entry(sum_entry(k1, "2"));
    db.add_entry(sum_entry(k2, "1.2"));
    db.add_entry(sum_entry(k2, "2.3"));
    db.add_entry(sum_entry(k3, "foo"));
    db.add_entry(sum_entry(k3, "bar"));

    EQ(db.size(), 3)

    auto e1 = entry_at(db, k1);
    EQ(e1.value(), "3")
    EQ(e1.has_tag("MERGED_SUM"), true)

    auto e2 = entry_at(db, k2);
    EQ(e2.value(), "3.5")
    EQ(e2.has_tag("MERGED_SUM"), true)

    // Not numbers: concatenated so nothing is lost, and tagged as failed
    auto e3 = entry_at(db, k3);
    EQ(e3.value(), "foo\nbar")
    EQ(e3.has_tag("MERGED"), true)
    EQ(e3.has_tag("MERGED_SUM_FAILED"), true)
    EQ(e3.has_tag("MERGED_SUM"), false)

    TEST_PASS
}

TEST(values_from_add_to_value_are_summed) {
    SeqDB db;
    dbkey_t key {3, 1, 1};

    // add_to_value() ends the value with '\n'; surrounding whitespace is ignored
    for (const char* v : {"10", "5"}) {
        DBEntry<> e;
        e.add_tag("total").add_tag("MERGE_STRATEGY=SUM").add_to_value(v);
        e.set_key(key);
        db.add_entry(move(e));
    }

    EQ(entry_at(db, key).value(), "15")

    TEST_PASS
}

TEST(large_integers_are_exact) {
    SeqDB db;
    dbkey_t key {3, 2, 1};

    // Beyond what a double holds exactly (2^53), e.g. amounts in wei
    db.add_entry(sum_entry(key, "1000000000000000000000"));
    db.add_entry(sum_entry(key, "1"));
    db.add_entry(sum_entry(key, "-2"));

    EQ(entry_at(db, key).value(), "999999999999999999999")

    TEST_PASS
}

TEST(small_decimals_are_kept) {
    SeqDB db;
    dbkey_t key {3, 3, 1};

    // Fixed six-decimal formatting would have stored "0.000000"
    db.add_entry(sum_entry(key, "1e-7"));
    db.add_entry(sum_entry(key, "1e-7"));

    EQ(entry_at(db, key).value(), "2e-07")

    TEST_PASS
}

TEST(both_entries_must_ask_for_sum) {
    SeqDB db;
    dbkey_t k1 {4, 1, 1};
    dbkey_t k2 {4, 2, 1};

    // Through add_entry: one SUM entry and one plain entry are merged normally
    db.add_entry(sum_entry(k1, "1"));
    {
        DBEntry<> e; e.add_tag("total"); e.value() = "2"; e.set_key(k1);
        db.add_entry(move(e));
    }

    // Through staging: same
    db.stage_add_entry(sum_entry(k2, "1"));
    {
        DBEntry<> e; e.add_tag("total"); e.value() = "2"; e.set_key(k2);
        db.stage_add_entry(move(e));
    }
    db.stage_close();

    for (auto key : {k1, k2}) {
        auto e = entry_at(db, key);
        EQ(e.has_tag("MERGED"), true)
        EQ(e.has_tag("MERGED_SUM"), false)
        bool concatenated = e.value() == "1\n2" || e.value() == "2\n1";
        EQ(concatenated, true)
    }

    TEST_PASS
}

TEST(merge_tags_are_indexed) {
    SeqDB db;
    dbkey_t sum_key {5, 1, 1};
    dbkey_t fail_key {5, 2, 1};

    db.add_entry(sum_entry(sum_key, "1"));
    db.add_entry(sum_entry(sum_key, "2"));
    db.add_entry(sum_entry(fail_key, "x"));
    db.add_entry(sum_entry(fail_key, "1"));

    auto summed = db.get_entry_by_tags(set<string> {"MERGED_SUM"});
    EQ(summed.size(), 1)
    EQ(summed.count(sum_key), 1)

    auto failed = db.get_entry_by_tags(set<string> {"MERGED_SUM_FAILED"});
    EQ(failed.size(), 1)
    EQ(failed.count(fail_key), 1)

    TEST_PASS
}

TEST(failed_key_warns_once) {
    SeqDB db;
    dbkey_t key {6, 1, 1};

    // Capture warnings
    stringstream captured;
    auto* old = cerr.rdbuf(captured.rdbuf());

    db.add_entry(sum_entry(key, "x"));
    db.add_entry(sum_entry(key, "1"));   // fails: warns
    db.add_entry(sum_entry(key, "2"));   // still fails (value is "x\n1"), no new warning
    db.add_entry(sum_entry(key, "3"));

    cerr.rdbuf(old);

    string log = captured.str();
    size_t warnings = 0;
    for (size_t pos = log.find("[WARNING]"); pos != string::npos; pos = log.find("[WARNING]", pos + 1))
        ++warnings;
    EQ(warnings, 1)

    auto e = entry_at(db, key);
    EQ(e.has_tag("MERGED_SUM_FAILED"), true)
    EQ(e.value().find("x") != string::npos, true)

    TEST_PASS
}

TESTS_BEGIN
    elga::ZMQChatterbox::Setup();
    RUN_TEST(identical_staged_entries_are_summed)
    RUN_TEST(different_staged_entries_are_summed)
    RUN_TEST(staged_entry_sums_with_existing_entry)
    RUN_TEST(separate_keys_stay_separate)
    RUN_TEST(add_entry_sums_integers_decimals_and_fails_on_text)
    RUN_TEST(values_from_add_to_value_are_summed)
    RUN_TEST(large_integers_are_exact)
    RUN_TEST(small_decimals_are_kept)
    RUN_TEST(both_entries_must_ask_for_sum)
    RUN_TEST(merge_tags_are_indexed)
    RUN_TEST(failed_key_warns_once)
    elga::ZMQChatterbox::Teardown();
TESTS_END
