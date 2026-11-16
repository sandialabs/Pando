#include <iostream>
#include <string>

#include "seq_db.hpp"

#include "test.hpp"

using namespace std;
using namespace pando;

// Entries tagged MERGE_STRATEGY=SUM that share a key must have their values
// added, whether they meet in the staging area (filters' make_new_entry during
// process()) or when a staged entry lands on one already in the DB.

static DBEntry<> count_entry(dbkey_t key, const string& value) {
    DBEntry<> e;
    e.add_tag("count").add_tag("MERGE_STRATEGY=SUM");
    // Set the value exactly, as make_new_entry does: add_to_value() appends a
    // '\n', which sum_entries cannot parse, so it would fall back to concatenating.
    e.value() = value;
    e.set_key(key);
    return e;
}

static double value_at(SeqDB& db, dbkey_t key) {
    auto entries = db.entries();
    auto it = entries.find(key);
    if (it == entries.end()) return -1;
    return stod(it->second.value());
}

TEST(identical_staged_entries_are_summed) {
    SeqDB db;
    dbkey_t key {KMER_COUNT, 12345, 678};

    // Identical "1" counts used to compare equal and the second was dropped
    db.stage_add_entry(count_entry(key, "1"));
    db.stage_add_entry(count_entry(key, "1"));
    db.stage_add_entry(count_entry(key, "1"));
    db.stage_close();

    EQ(db.size(), 1)
    EQ(value_at(db, key), 3.0)
    TEST_PASS
}

TEST(different_staged_entries_are_summed) {
    SeqDB db;
    dbkey_t key {KMER_COUNT, 1, 2};

    // Differing values used to be concatenated ("2\n5")
    db.stage_add_entry(count_entry(key, "2"));
    db.stage_add_entry(count_entry(key, "5"));
    db.stage_close();

    EQ(value_at(db, key), 7.0)
    TEST_PASS
}

TEST(staged_entry_sums_with_existing_entry) {
    SeqDB db;
    dbkey_t key {KMER_COUNT, 9, 9};

    db.add_entry(count_entry(key, "4"));
    db.stage_add_entry(count_entry(key, "1"));
    db.stage_add_entry(count_entry(key, "1"));
    db.stage_close();

    EQ(db.size(), 1)
    EQ(value_at(db, key), 6.0)
    TEST_PASS
}

TEST(separate_keys_stay_separate) {
    SeqDB db;
    dbkey_t k1 {KMER_COUNT, 100, 0};
    dbkey_t k2 {KMER_COUNT, 100, 1};

    db.stage_add_entry(count_entry(k1, "1"));
    db.stage_add_entry(count_entry(k2, "1"));
    db.stage_add_entry(count_entry(k1, "1"));
    db.stage_close();

    EQ(db.size(), 2)
    EQ(value_at(db, k1), 2.0)
    EQ(value_at(db, k2), 1.0)
    TEST_PASS
}

TESTS_BEGIN
    elga::ZMQChatterbox::Setup();
    RUN_TEST(identical_staged_entries_are_summed)
    RUN_TEST(different_staged_entries_are_summed)
    RUN_TEST(staged_entry_sums_with_existing_entry)
    RUN_TEST(separate_keys_stay_separate)
    elga::ZMQChatterbox::Teardown();
TESTS_END
