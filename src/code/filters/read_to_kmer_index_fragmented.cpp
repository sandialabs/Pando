#include "helpers.hpp"
#include <sstream>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cctype>

using namespace std;
using namespace pando;
using namespace rapidjson;

#define FILTER_NAME "read_kmer_index_frag"
#define K 31
#define FRAG 4000
#define OVERLAP 2000

static const char* filter_done_tag = FILTER_NAME ":done";
static const char* filter_fail_tag = FILTER_NAME ":fail";

template<typename ...Args>
void fail_(const DBAccess *access, const char* filter_fail_tag, const char* filter_name, Args && ...args) {
    cerr << "[ERROR] unable to run filter: " << filter_name;
    (cerr << " " << ... << args);
    cerr << endl;
    access->add_tag.run(&access->add_tag, filter_fail_tag);
}

bool is_uint32_string(const string& s) {
    if (s.empty())
        return false;

    for (unsigned char c : s) {
        if (!isdigit(c))
            return false;
    }

    return true;
}
void read_kmer_index_frag(const DBAccess *access, const char* filter_fail_tag, const char* filter_done_tag)
{
    string val = string(access->value);
    vector<string> lines;
    split_by_newline(val, &lines);

    // Expected VALUE layout:
    // lines[0] = read_id
    // lines[1] = sequence
    // lines[2] = +
    // lines[3] = quality
	if (lines.size() < 4)
	{
		cerr << "Expected at least 4 lines in shortread entry, got " << lines.size() << endl;
		access->add_tag.run(&access->add_tag, filter_fail_tag);
		return;
	}

	string read_id = lines[0];
	read_id.erase(std::remove_if(read_id.begin(), read_id.end(), [](unsigned char c) {
		return std::isspace(c);
	}), read_id.end());

	string seq = lines[1];
	seq.erase(std::remove_if(seq.begin(), seq.end(), [](unsigned char c) {
		return std::isspace(c);
	}), seq.end());

    if (seq.empty())
    {
        cerr << "Empty sequence for read id: '" << read_id << "'" << endl;
        access->add_tag.run(&access->add_tag, filter_fail_tag);
        return;
    }

    if (!is_uint32_string(read_id))
    {
        cerr << "Invalid read id in lines[0]: '" << read_id << "'" << endl;
        access->add_tag.run(&access->add_tag, filter_fail_tag);
        return;
    }

//     uint32_t read_id_key;

//     try
//     {
//         unsigned long long parsed_id = stoull(read_id);

//         if (parsed_id > numeric_limits<uint32_t>::max())
//         {
//             cerr << "Read id exceeds 32 bits: '" << read_id << "'" << endl;
//             access->add_tag.run(&access->add_tag, filter_fail_tag);
//             return;
//         }

//         read_id_key = static_cast<uint32_t>(parsed_id);
//     }
//     catch (const exception& e)
//     {
//         cerr << "Unable to parse read id from lines[4]: '" << read_id << "'" << endl;
//         cerr << "Error: " << e.what() << endl;
//         access->add_tag.run(&access->add_tag, filter_fail_tag);
//         return;
//     }
    try
    {
        unsigned long long parsed_id = stoull(read_id);

        if (parsed_id > numeric_limits<uint32_t>::max())
        {
            cerr << "Read id exceeds 32 bits: '" << read_id << "'" << endl;
            access->add_tag.run(&access->add_tag, filter_fail_tag);
            return;
        }
    }
    catch (const exception& e)
    {
        cerr << "Unable to parse read id from lines[0]: '" << read_id << "'" << endl;
        cerr << "Error: " << e.what() << endl;
        access->add_tag.run(&access->add_tag, filter_fail_tag);
        return;
    }

    /* ADD NEW ENTRY TO SEQUENCE DATASET */

//     const char* read_sequence_tags[] = {"seq", ""};
//     dbkey_t read_sequence_key = make_32bit_table_key(SEQ, read_id_key);
//     access->make_new_entry.run(&access->make_new_entry, read_sequence_tags, seq.c_str(), read_sequence_key);

//      if (seq.length() < K)
//      {
//          access->add_tag.run(&access->add_tag, filter_done_tag);
//          return;
//      }

//     unordered_map<string, vector<int>> kmer_positions;

//     if (OVERLAP >= FRAG)
//     {
//         access->add_tag.run(&access->add_tag, filter_fail_tag);
//         return;
//     }

//     const size_t frag_step = FRAG - OVERLAP;

//     uint32_t fragment_number = 0;

//     for (size_t frag_start = 0; frag_start < seq.length(); frag_start += frag_step, ++fragment_number)
//     {
//         size_t frag_end = min(frag_start + static_cast<size_t>(FRAG), seq.length());

//         if (frag_end - frag_start < K)
//             continue;

//         for (size_t i = frag_start; i + K <= frag_end; ++i)
//         {
//             string kmer = seq.substr(i, K);
//             long long b_key;

//             try
//             {
//                 b_key = sequence_to_bits(kmer);
//             }
//             catch (const runtime_error& e)
//             {
//                 cerr << "Error with " << e.what() << endl;
//                 cerr << "Attempting to do kmer: '" << kmer << "'" << endl;
//                 access->add_tag.run(&access->add_tag, filter_fail_tag);
//                 return;
//             }

//             if (b_key == -1)
//             {
//                 cerr << "Invalid nucleotide in kmer: '" << kmer << "'" << endl;
//                 access->add_tag.run(&access->add_tag, filter_fail_tag);
//                 return;
//             }

//             uint64_t encoded_kmer = static_cast<uint64_t>(b_key);
//             vector<uint32_t>& fragments = kmer_fragments[encoded_kmer];

// 			fragments.push_back(fragment_number);
//     }
    if (seq.length() < K)
    {
        access->add_tag.run(&access->add_tag, filter_done_tag);
        return;
    }

    if (OVERLAP >= FRAG)
    {
        access->add_tag.run(&access->add_tag, filter_fail_tag);
        return;
    }

    const size_t frag_step = FRAG - OVERLAP;

    unordered_map<uint64_t, vector<uint32_t>> kmer_fragments;

    uint32_t fragment_number = 0;

    for (size_t frag_start = 0; frag_start < seq.length(); frag_start += frag_step, ++fragment_number)
    {
        size_t frag_end = min(frag_start + static_cast<size_t>(FRAG), seq.length());

        if (frag_end - frag_start < K)
            continue;

        for (size_t i = frag_start; i + K <= frag_end; ++i)
        {
            string kmer = seq.substr(i, K);
            long long b_key;

            try
            {
                b_key = sequence_to_bits(kmer);
            }
            catch (const runtime_error& e)
            {
                cerr << "Error with " << e.what() << endl;
                cerr << "Attempting to do kmer: '" << kmer << "'" << endl;
                access->add_tag.run(&access->add_tag, filter_fail_tag);
                return;
            }

            if (b_key == -1)
            {
                cerr << "Invalid nucleotide in kmer: '" << kmer << "'" << endl;
                access->add_tag.run(&access->add_tag, filter_fail_tag);
                return;
            }

            uint64_t encoded_kmer = static_cast<uint64_t>(b_key);
            kmer_fragments[encoded_kmer].push_back(fragment_number);
        }
    }

    const char* kmer_index_tags[] = {"index", ""};

    for (const auto& pair : kmer_fragments)
    {
        uint64_t encoded_kmer = pair.first;
        const vector<uint32_t>& fragments = pair.second;

        dbkey_t new_key = make_kmer_index_key(encoded_kmer);

		string new_value = read_id + ":";

		bool first_fragment = true;

		for (size_t j = 0; j < fragments.size();)
		{
			uint32_t fragment = fragments[j];
			uint64_t count = 1;
			++j;

			while (j < fragments.size() && fragments[j] == fragment)
			{
				++count;
				++j;
			}

			if (!first_fragment)
				new_value += ",";

			first_fragment = false;

			new_value += to_string(fragment);

			if (count > 1)
			{
				new_value += "*";
				new_value += to_string(count);
			}
		}
//         string new_value = read_id + ":";

//         for (size_t j = 0; j < fragments.size(); ++j)
//         {
//             if (j > 0)
//                 new_value += ",";
//             new_value += to_string(fragments[j]);
//         }

        access->make_new_entry.run(&access->make_new_entry, kmer_index_tags, new_value.c_str(), new_key);
    }

//     access->add_tag.run(&access->add_tag, filter_done_tag);

//     const char* kmer_index_tags[] = {"index", ""};

//     for (const auto& pair : kmer_fragments)
//     {
//         uint64_t encoded_kmer = pair.first;
//         const vector<uint32_t>& fragments = pair.second;

//         dbkey_t new_key = make_kmer_index_key(encoded_kmer);

//         string new_value = read_id + ":";

//         for (size_t j = 0; j < fragments.size(); ++j)
//         {
//             if (j > 0)
//                 new_value += ",";
//             new_value += to_string(fragments[j]);
//         }

//         access->make_new_entry.run(&access->make_new_entry, kmer_index_tags, new_value.c_str(), new_key);
//     }

    access->add_tag.run(&access->add_tag, filter_done_tag);
}

bool should_run_read_kmer_index_frag(const DBAccess* access, const char* filter_fail_tag, const char* filter_done_tag) {
    auto tags = access->tags;
    bool found_read = false;

    while ((*tags)[0] != '\0') {
        if (string(*tags) == "read")
            found_read = true;
        if (string(*tags) == filter_done_tag)
            return false;
        if (string(*tags) == filter_fail_tag)
            return false;
        ++tags;
    }

    return found_read;
}

void run_(const DBAccess *access) {
    return read_kmer_index_frag(access, filter_fail_tag, filter_done_tag);
}

// Fit the Pando API
extern "C" {
    /** @brief Main filter entry point */
    extern void run(void* access) {
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        return should_run_read_kmer_index_frag(access, filter_fail_tag, filter_done_tag);
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
