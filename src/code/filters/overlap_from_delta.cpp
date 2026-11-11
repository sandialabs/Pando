#include "helpers.hpp"
#include <sstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cctype>
#include <cstdlib>

using namespace std;
using namespace pando;
using namespace rapidjson;

#define FILTER_NAME "overlap_from_delta"

static const char* filter_done_tag = FILTER_NAME ":done";
static const char* filter_fail_tag = FILTER_NAME ":fail";

void overlap_from_delta(const DBAccess *access, const char* filter_fail_tag, const char* filter_done_tag) {
    string val = string(access->value);
    vector<string> lines;
    split_by_newline(val, &lines);

    if (lines.size() < 1)
    {
        cerr << "Delta entry is empty" << endl;
        access->add_tag.run(&access->add_tag, filter_fail_tag);
        return;
    }

    string self_read_id_string = lines[0];
    self_read_id_string.erase(std::remove_if(self_read_id_string.begin(), self_read_id_string.end(), [](unsigned char c) { return std::isspace(c); }), self_read_id_string.end());

    if (self_read_id_string.empty())
    {
        cerr << "Delta entry has empty read id" << endl;
        access->add_tag.run(&access->add_tag, filter_fail_tag);
        return;
    }

    uint32_t self_read_id;

    try
    {
        unsigned long long parsed_id = stoull(self_read_id_string);

        if (parsed_id > numeric_limits<uint32_t>::max())
        {
            cerr << "Read id exceeds 32 bits: '" << self_read_id_string << "'" << endl;
            access->add_tag.run(&access->add_tag, filter_fail_tag);
            return;
        }

        self_read_id = static_cast<uint32_t>(parsed_id);
    }
    catch (const exception& e)
    {
        cerr << "Unable to parse read id from delta entry: '" << self_read_id_string << "'" << endl;
        cerr << "Error: " << e.what() << endl;
        access->add_tag.run(&access->add_tag, filter_fail_tag);
        return;
    }

    const char* candidate_tags[] = {"candidate", ""};

    for (size_t i = 1; i < lines.size(); ++i)
    {
        string encoded_kmer_string = lines[i];
        encoded_kmer_string.erase(std::remove_if(encoded_kmer_string.begin(), encoded_kmer_string.end(), [](unsigned char c) { return std::isspace(c); }), encoded_kmer_string.end());

        if (encoded_kmer_string.empty())
            continue;

        uint64_t encoded_kmer;

        try
        {
            encoded_kmer = stoull(encoded_kmer_string);
        }
        catch (const exception& e)
        {
            cerr << "Unable to parse encoded kmer from delta entry: '" << encoded_kmer_string << "'" << endl;
            cerr << "Error: " << e.what() << endl;
            access->add_tag.run(&access->add_tag, filter_fail_tag);
            return;
        }

        dbkey_t kmer_key = make_kmer_index_key(encoded_kmer);
        char* bucket_ret = nullptr;
        access->get_entry_by_key.run(&access->get_entry_by_key, kmer_key, &bucket_ret);

        if (bucket_ret == nullptr)
        {
            // Kmer bucket is not visible yet. Process again later.
            return;
        }

        string bucket { bucket_ret };
        free(bucket_ret);

        vector<string> bucket_lines;
        split_by_newline(bucket, &bucket_lines);

        for (const string& raw_bucket_line : bucket_lines)
        {
            string bucket_line = raw_bucket_line;
            bucket_line.erase(std::remove_if(bucket_line.begin(), bucket_line.end(), [](unsigned char c) { return std::isspace(c); }), bucket_line.end());

            if (bucket_line.empty())
                continue;

            size_t colon_pos = bucket_line.find(':');

            if (colon_pos == string::npos)
            {
                cerr << "Malformed kmer index bucket line: '" << bucket_line << "'" << endl;
                access->add_tag.run(&access->add_tag, filter_fail_tag);
                return;
            }

            string other_read_id_string = bucket_line.substr(0, colon_pos);
            uint32_t other_read_id;

            try
            {
                unsigned long long parsed_other_id = stoull(other_read_id_string);

                if (parsed_other_id > numeric_limits<uint32_t>::max())
                {
                    cerr << "Other read id exceeds 32 bits: '" << other_read_id_string << "'" << endl;
                    access->add_tag.run(&access->add_tag, filter_fail_tag);
                    return;
                }

                other_read_id = static_cast<uint32_t>(parsed_other_id);
            }
            catch (const exception& e)
            {
                cerr << "Unable to parse other read id from bucket line: '" << bucket_line << "'" << endl;
                cerr << "Error: " << e.what() << endl;
                access->add_tag.run(&access->add_tag, filter_fail_tag);
                return;
            }

            if (other_read_id == self_read_id)
                continue;

            string positions = bucket_line.substr(colon_pos + 1);

            dbkey_t candidate_key = {OVERLAP_CANDIDATES, static_cast<uint16_t>(rand() & LOW_16_BITS_MASK), static_cast<uint16_t>(rand() & LOW_16_BITS_MASK)};
            string candidate_value = self_read_id_string + "," + other_read_id_string + "\n" + positions;
            access->make_new_entry.run(&access->make_new_entry, candidate_tags, candidate_value.c_str(), candidate_key);
        }
    }

    access->add_tag.run(&access->add_tag, filter_done_tag);
}

bool should_run_overlap_from_delta(const DBAccess* access, const char* filter_fail_tag, const char* filter_done_tag) {
    auto tags = access->tags;
    bool found_delta = false;

    while ((*tags)[0] != '\0') {
        if (string(*tags) == "delta")
            found_delta = true;
        if (string(*tags) == filter_done_tag)
            return false;
        if (string(*tags) == filter_fail_tag)
            return false;
        ++tags;
    }

    if (found_delta)
        return true;

    return false;
}

void run_(const DBAccess *access) {
    return overlap_from_delta(access, filter_fail_tag, filter_done_tag);
}

// Fit the Pando API
extern "C" {
    /** @brief Main filter entry point */
    extern void run(void* access) {
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        return should_run_overlap_from_delta(access, filter_fail_tag, filter_done_tag);
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
