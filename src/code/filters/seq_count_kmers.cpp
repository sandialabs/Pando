#include "helpers.hpp"
#include <sstream>
#include <unordered_map>
#include <stdexcept>
using namespace std;
using namespace pando;
using namespace rapidjson;

#define FILTER_NAME "seq_count_kmers"
#define K 15
static const char* filter_done_tag = FILTER_NAME ":done";
static const char* filter_fail_tag = FILTER_NAME ":fail";


template<typename ...Args>
void fail_(const DBAccess *access, const char* filter_fail_tag, const char* filter_name, Args && ...args) {
    cerr << "[ERROR] unable to run filter: " << filter_name;
    (cerr << " " << ... << args);
    cerr << endl;
    access->add_tag.run(&access->add_tag, filter_fail_tag);
}

void seq_count_kmers(const DBAccess *access, const char* filter_fail_tag, const char* filter_done_tag) {
    // Create new DB entries for each transaction
    string val = string(access->value);
    val.erase(std::remove_if(val.begin(), val.end(), [](unsigned char c) { return std::isspace(c); }), val.end());
    const char* new_tags[] = {"kmer_count", "k=15", "MERGE_STRATEGY_SUM", ""};
    if (val.empty())
    {
        access->add_tag.run(&access->add_tag, filter_fail_tag);
        return;
    }
    unordered_map<string, int> kmer_counts;
    for (size_t i = 0; i <= val.length() - K; ++i)
    {
        string kmer = val.substr(i, K); // Extract k-mer
        kmer_counts[kmer]++; // Increment the count for this k-mer
    }
    long long b_key;
    dbkey_t new_key;
    string new_value;
    for (const auto& pair : kmer_counts)
    {   
        try
        {
            b_key = sequence_to_bits(pair.first);
        }
        catch (const runtime_error& e)
        {
            cerr << "Error with " << e.what() << endl;
            cerr << "Attempting to do kmer: '" << pair.first << "'" << endl;
            throw runtime_error("error kmer!");
        }
        if (b_key == -1)
        {
            cerr << "Invalid nucleotide in kmer: '" << pair.first << "'" << endl;
            access->add_tag.run(&access->add_tag, filter_fail_tag);
            return;
        }
        new_key = {KMER_COUNTS, b_key, 0};
        new_value = to_string(pair.second);
        access->make_new_entry.run(&access->make_new_entry, new_tags, new_value.c_str(), new_key);
    }

    // Add a tag indicating parsing was successful
    access->add_tag.run(&access->add_tag, filter_done_tag);
}


bool should_run_seq_count_kmers(const DBAccess* access, const char* filter_fail_tag, const char* filter_done_tag) {
    auto tags = access->tags;
    bool found_read = false;

    while ((*tags)[0] != '\0') {
        if (string(*tags) == "seq")
            found_read = true;
        if (string(*tags) == filter_done_tag)
            return false;
        if (string(*tags) == filter_fail_tag)
            return false;
        ++tags;
    }
    if (found_read)
        return true;
    return false;
}


void run_(const DBAccess *access) {
    return seq_count_kmers(access, filter_fail_tag, filter_done_tag);
}


// Fit the Pando API
extern "C" {
    /** @brief Main filter entry point */
    extern void run(void* access) {
        // Run internally
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        return should_run_seq_count_kmers(access, filter_fail_tag, filter_done_tag);
    }

    /** @brief Contains the entry point and tags for the filter */
    extern const FilterInterface filter {
        filter_name: FILTER_NAME,
        filter_type: SINGLE_ENTRY,
        should_run: &should_run,
        init: nullptr,
        destroy: nullptr,
        run: &run
    };
}
