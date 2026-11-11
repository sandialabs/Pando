#include <iostream>
#include <string>

#include "seq_db.hpp"

#include "test.hpp"

#include <chrono>

using namespace std;
using namespace pando;
using namespace std::chrono;

TEST(tag_added) {
    // Load the test data
    SeqDB db { data_dir+"/simple_fastq.txt" };

    db.add_filter_dir(build_dir+"/filters");

    db.install_filter("seq_kmer_index");

    for (auto & [key, entry] : db.entries())
        EQ(entry.has_tag("seq_kmer_index:done"), false);

    db.process();

    for (auto & [key, entry] : db.entries())
        if (entry.has_tag("shortread"))
            EQ(entry.has_tag("seq_kmer_index:done"), true);

    TEST_PASS
}

TEST(tag_not_added) {
    SeqDB db { data_dir+"/simple_fastq.txt" };
    DBEntry<> n;
    n.add_tag("seq");
    n.add_tag("x");
    n.add_to_value("some value");
    db.add_entry(n);

    db.add_filter_dir(build_dir + "/filters");

    db.install_filter("read_kmer_index");

    for (auto & [key, entry] : db.entries())
        EQ(entry.has_tag("seq_kmer_index:done"), false);

    db.process();

    for (auto & [key, entry] : db.entries()) {
        if (entry.has_tag("x") || entry.has_tag("kmer_index"))
            EQ(entry.has_tag("read_kmer_index:done"), false)
        else
            EQ(entry.has_tag("read_kmer_index:done"), true)
    }

    TEST_PASS
}

TEST(exclude_run) {
    SeqDB db { data_dir+"/simple_fastq.txt" };

    db.add_filter_dir(build_dir + "/filters");

    db.install_filter("read_kmer_index");

    EQ(db.size(), 1);
    db.process();
    EQ(db.size(), 2);

    db.process();
    EQ(db.size(), 2);
    DBEntry<> e;
    e.add_tag("human").add_tag("shortread").add_to_value("@chr1_557_12314_0:0:0_0:0:0_1/1\n").add_to_value("AGCD");
    db.add_entry(e);
    EQ(db.size(), 3);
    db.process();

    EQ(db.size(), 4);
    for (auto & [key, entry] : db.entries()) {
        EQ(entry.has_tag("read_kmer_index:fail"), false);
    }

    e.add_tag("read_to_seq:fail");
    db.add_entry(e);
    EQ(db.size(), 5);

    db.process();

    EQ(db.size(), 5);

    TEST_PASS
}


TEST(seq_created) {
    SeqDB db { data_dir+"/simple_fastq.txt" };
    db.add_filter_dir(build_dir + "/filters");
    db.install_filter("read_kmer_index");
    cout << "Before" << endl;
    for (auto & [key, entry] : db.entries()) {
        cout << entry << endl;
    } 
//     EQ(db.size(), 1);
    db.process();
//     EQ(db.size(), 2);
    cout << "After" << endl;
    for (auto & [key, entry] : db.entries()) {
        cout << entry << endl;
    } 

    TEST_PASS
}


TESTS_BEGIN
    elga::ZMQChatterbox::Setup();
    // RUN_TEST(tag_added)
    // RUN_TEST(tag_not_added)
    // RUN_TEST(exclude_run)
    RUN_TEST(seq_created)
    elga::ZMQChatterbox::Teardown();
TESTS_END
