#include <iostream>
#include <string>
#include <algorithm>

#include "seq_db.hpp"
#include "test.hpp"

using namespace std;
using namespace pando;

#define EXPECTED_READS 2
#define EXPECTED_SEQ_ENTRIES 2
#define EXPECTED_DELTA_ENTRIES 2
#define EXPECTED_INDEX_BUCKETS 13

size_t line_count(const string& s) {
    if (s.empty())
        return 0;
    return count(s.begin(), s.end(), '\n') + 1;
}

DBEntry<> make_read_entry(const string& name, const string& seq, uint32_t read_id) {
    DBEntry<> e;
    e.add_tag("read");
    e.add_to_value("@" + name + "\n" + seq + "\n+\n" + string(seq.size(), 'I') + "\n" + to_string(read_id));
    return e;
}

TEST(read_kmer_index_entries_created) {
    SeqDB db;

    string seq1 = string(31, 'A') + string(4, 'C');
    string seq2 = string(4, 'T') + string(31, 'A') + string(4, 'G');

    db.add_entry(make_read_entry("read1", seq1, 1));
    db.add_entry(make_read_entry("read2", seq2, 2));

    db.add_filter_dir(build_dir + "/filters");
    db.install_filter("read_kmer_index");

    EQ(db.size(), EXPECTED_READS);

    db.process();

    EQ(db.size(), EXPECTED_READS + EXPECTED_SEQ_ENTRIES + EXPECTED_DELTA_ENTRIES + EXPECTED_INDEX_BUCKETS);

    size_t read_count = 0;
    size_t seq_count = 0;
    size_t delta_count = 0;
    size_t index_count = 0;

    bool seq1_found = false;
    bool seq2_found = false;

    bool delta1_found = false;
    bool delta2_found = false;

    bool shared_a31_bucket_found = false;

    for (auto & [key, entry] : db.entries()) {
        EQ(entry.has_tag("read_kmer_index:fail"), false);

        if (entry.has_tag("read")) {
            read_count++;
            EQ(entry.has_tag("read_kmer_index:done"), true);
        }

        if (entry.has_tag("seq")) {
            seq_count++;
            if (entry.value() == seq1)
                seq1_found = true;
            if (entry.value() == seq2)
                seq2_found = true;
        }

        if (entry.has_tag("delta")) {
            delta_count++;

            if (entry.value().rfind("1\n", 0) == 0) {
                delta1_found = true;
                EQ(line_count(entry.value()), 6);  // read id + 5 unique kmers
            }

            if (entry.value().rfind("2\n", 0) == 0) {
                delta2_found = true;
                EQ(line_count(entry.value()), 10); // read id + 9 unique kmers
            }
        }

        if (entry.has_tag("index")) {
            index_count++;

            if (entry.value().find("1:0") != string::npos && entry.value().find("2:4") != string::npos)
                shared_a31_bucket_found = true;
        }
    }

    EQ(read_count, EXPECTED_READS);
    EQ(seq_count, EXPECTED_SEQ_ENTRIES);
    EQ(delta_count, EXPECTED_DELTA_ENTRIES);
    EQ(index_count, EXPECTED_INDEX_BUCKETS);

    EQ(seq1_found, true);
    EQ(seq2_found, true);

    EQ(delta1_found, true);
    EQ(delta2_found, true);

    EQ(shared_a31_bucket_found, true);

    TEST_PASS
}

TEST(overlap_candidates_created_from_delta) {
    SeqDB db;

    string seq1 = string(31, 'A') + string(4, 'C');
    string seq2 = string(4, 'T') + string(31, 'A') + string(4, 'G');

    db.add_entry(make_read_entry("read1", seq1, 1));
    db.add_entry(make_read_entry("read2", seq2, 2));

    db.add_filter_dir(build_dir + "/filters");
    db.install_filter("read_kmer_index");
    db.install_filter("overlap_from_delta");

    db.process();
    db.process();
    db.process();

    size_t candidate_count = 0;
    bool candidate_1_2_found = false;
    bool candidate_2_1_found = false;
    bool self_candidate_found = false;

    for (auto & [key, entry] : db.entries()) {
        EQ(entry.has_tag("read_kmer_index:fail"), false);
        EQ(entry.has_tag("overlap_from_delta:fail"), false);

        if (entry.has_tag("candidate")) {
            candidate_count++;

            if (entry.value() == "1,2\n4")
                candidate_1_2_found = true;

            if (entry.value() == "2,1\n0")
                candidate_2_1_found = true;

            if (entry.value().find("1,1") != string::npos || entry.value().find("2,2") != string::npos)
                self_candidate_found = true;
        }
    }

    EQ(candidate_count >= 1, true);
    EQ(candidate_1_2_found || candidate_2_1_found, true);
    EQ(self_candidate_found, false);

    TEST_PASS
}

TEST(short_read_gets_seq_but_no_delta_or_index) {
    SeqDB db;

    string short_seq = string(10, 'A');

    db.add_entry(make_read_entry("short_read", short_seq, 1));

    db.add_filter_dir(build_dir + "/filters");
    db.install_filter("read_kmer_index");

    db.process();

    size_t read_count = 0;
    size_t seq_count = 0;
    size_t delta_count = 0;
    size_t index_count = 0;

    bool short_seq_found = false;

    for (auto & [key, entry] : db.entries()) {
        EQ(entry.has_tag("read_kmer_index:fail"), false);

        if (entry.has_tag("read")) {
            read_count++;
            EQ(entry.has_tag("read_kmer_index:done"), true);
        }

        if (entry.has_tag("seq")) {
            seq_count++;
            if (entry.value() == short_seq)
                short_seq_found = true;
        }

        if (entry.has_tag("delta"))
            delta_count++;

        if (entry.has_tag("index"))
            index_count++;
    }

    EQ(read_count, 1);
    EQ(seq_count, 1);
    EQ(delta_count, 0);
    EQ(index_count, 0);
    EQ(short_seq_found, true);

    TEST_PASS
}

TEST(bad_read_missing_id_fails) {
    SeqDB db;

    DBEntry<> e;
    e.add_tag("read");
    e.add_to_value("@bad_read\n" + string(31, 'A') + "\n+\n" + string(31, 'I'));
    db.add_entry(e);

    db.add_filter_dir(build_dir + "/filters");
    db.install_filter("read_kmer_index");

    db.process();

    size_t read_count = 0;
    size_t seq_count = 0;
    size_t delta_count = 0;
    size_t index_count = 0;

    for (auto & [key, entry] : db.entries()) {
        if (entry.has_tag("read")) {
            read_count++;
            EQ(entry.has_tag("read_kmer_index:done"), false);
            EQ(entry.has_tag("read_kmer_index:fail"), true);
        }

        if (entry.has_tag("seq"))
            seq_count++;

        if (entry.has_tag("delta"))
            delta_count++;

        if (entry.has_tag("index"))
            index_count++;
    }

    EQ(read_count, 1);
    EQ(seq_count, 0);
    EQ(delta_count, 0);
    EQ(index_count, 0);

    TEST_PASS
}

TESTS_BEGIN
    elga::ZMQChatterbox::Setup();
    RUN_TEST(read_kmer_index_entries_created)
    RUN_TEST(overlap_candidates_created_from_delta)
    RUN_TEST(short_read_gets_seq_but_no_delta_or_index)
    RUN_TEST(bad_read_missing_id_fails)
    elga::ZMQChatterbox::Teardown();
TESTS_END
