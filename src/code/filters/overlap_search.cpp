#include "helpers.hpp"
#include <sstream>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cctype>
#include <chrono>
#include <thread>

using namespace std;
using namespace pando;
using namespace rapidjson;

#define FILTER_NAME "overlap_search"
#define K 31
#define FRAG 4000
#define OVERLAP 2000
#define THRESHOLD 0.1

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

void overlap_search(const DBAccess *access, const char* filter_fail_tag, const char* filter_done_tag)
{
    string val(access->value);
    vector<string> lines;
    split_by_newline(val, &lines);

    if (lines.size() < 4)
    {
        cerr << "Expected at least 4 lines in read entry, got " << lines.size() << endl;
        access->add_tag.run(&access->add_tag, filter_fail_tag);
        return;
    }

    string query_read_id_str = lines[0];
    query_read_id_str.erase(remove_if(query_read_id_str.begin(), query_read_id_str.end(), [](unsigned char c) {
        return isspace(c);
    }), query_read_id_str.end());

    string seq = lines[1];
    seq.erase(remove_if(seq.begin(), seq.end(), [](unsigned char c) {
        return isspace(c);
    }), seq.end());

    if (query_read_id_str.empty() || seq.length() < K)
    {
        access->add_tag.run(&access->add_tag, filter_done_tag);
        return;
    }

    uint32_t query_read_id = 0;

    {
        uint64_t parsed = 0;

        for (unsigned char c : query_read_id_str)
        {
            if (!isdigit(c))
            {
                cerr << "Invalid query read id: '" << query_read_id_str << "'" << endl;
                access->add_tag.run(&access->add_tag, filter_fail_tag);
                return;
            }

            parsed = parsed * 10 + static_cast<uint64_t>(c - '0');

            if (parsed > numeric_limits<uint32_t>::max())
            {
                cerr << "Query read id exceeds uint32: '" << query_read_id_str << "'" << endl;
                access->add_tag.run(&access->add_tag, filter_fail_tag);
                return;
            }
        }

        query_read_id = static_cast<uint32_t>(parsed);
    }

    const uint64_t total_query_kmers = static_cast<uint64_t>(seq.length() - K + 1);

    vector<uint64_t> query_kmers;
    query_kmers.reserve(total_query_kmers);

    string kmer;
    kmer.reserve(K);

    for (size_t i = 0; i + K <= seq.length(); ++i)
    {
        kmer.assign(seq, i, K);

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

        query_kmers.push_back(static_cast<uint64_t>(b_key));
    }

    sort(query_kmers.begin(), query_kmers.end());

    struct QueryKmer
    {
        uint64_t encoded_kmer;
        uint64_t multiplicity;
    };

    vector<QueryKmer> unique_query_kmers;
    unique_query_kmers.reserve(query_kmers.size());

    for (size_t i = 0; i < query_kmers.size();)
    {
        uint64_t encoded_kmer = query_kmers[i];
        uint64_t multiplicity = 1;
        ++i;

        while (i < query_kmers.size() && query_kmers[i] == encoded_kmer)
        {
            ++multiplicity;
            ++i;
        }

        unique_query_kmers.push_back({encoded_kmer, multiplicity});
    }

    unordered_map<uint64_t, uint64_t> read_fragment_counts;
    read_fragment_counts.reserve(unique_query_kmers.size() * 16);

    uint32_t best_read_id = 0;
    uint32_t best_fragment = 0;
    uint64_t best_count = 0;

    auto process_index_value = [&](const char* s, size_t n, uint64_t query_multiplicity) -> bool
    {
        size_t p = 0;
        uint64_t parsed_read_id = 0;
        bool saw_read_digit = false;

        while (p < n && s[p] != ':')
        {
            unsigned char c = static_cast<unsigned char>(s[p]);

            if (isspace(c))
            {
                ++p;
                continue;
            }

            if (!isdigit(c))
            {
                cerr << "Invalid index value: '";
                cerr.write(s, n);
                cerr << "'" << endl;
                return false;
            }

            saw_read_digit = true;
            parsed_read_id = parsed_read_id * 10 + static_cast<uint64_t>(c - '0');

            if (parsed_read_id > numeric_limits<uint32_t>::max())
            {
                cerr << "Invalid index value: '";
                cerr.write(s, n);
                cerr << "'" << endl;
                return false;
            }

            ++p;
        }

        if (!saw_read_digit || p >= n || s[p] != ':')
        {
            cerr << "Invalid index value: '";
            cerr.write(s, n);
            cerr << "'" << endl;
            return false;
        }

        uint32_t hit_read_id = static_cast<uint32_t>(parsed_read_id);

        if (hit_read_id == query_read_id)
            return true;

        ++p;

        while (p < n)
        {
            while (p < n && isspace(static_cast<unsigned char>(s[p])))
                ++p;

            if (p >= n)
                break;

            uint64_t parsed_fragment = 0;
            bool saw_fragment_digit = false;

            while (p < n && s[p] != ',' && s[p] != '*')
            {
                unsigned char c = static_cast<unsigned char>(s[p]);

                if (isspace(c))
                {
                    ++p;
                    continue;
                }

                if (!isdigit(c))
                {
                    cerr << "Invalid index value: '";
                    cerr.write(s, n);
                    cerr << "'" << endl;
                    return false;
                }

                saw_fragment_digit = true;
                parsed_fragment = parsed_fragment * 10 + static_cast<uint64_t>(c - '0');

                if (parsed_fragment > numeric_limits<uint32_t>::max())
                {
                    cerr << "Invalid index value: '";
                    cerr.write(s, n);
                    cerr << "'" << endl;
                    return false;
                }

                ++p;
            }

            if (!saw_fragment_digit)
            {
                cerr << "Invalid index value: '";
                cerr.write(s, n);
                cerr << "'" << endl;
                return false;
            }

            uint64_t index_count = 1;

            if (p < n && s[p] == '*')
            {
                ++p;

                index_count = 0;
                bool saw_count_digit = false;

                while (p < n && s[p] != ',')
                {
                    unsigned char c = static_cast<unsigned char>(s[p]);

                    if (isspace(c))
                    {
                        ++p;
                        continue;
                    }

                    if (!isdigit(c))
                    {
                        cerr << "Invalid index value: '";
                        cerr.write(s, n);
                        cerr << "'" << endl;
                        return false;
                    }

                    saw_count_digit = true;
                    index_count = index_count * 10 + static_cast<uint64_t>(c - '0');

                    if (index_count == 0)
                    {
                        cerr << "Invalid index value: '";
                        cerr.write(s, n);
                        cerr << "'" << endl;
                        return false;
                    }

                    ++p;
                }

                if (!saw_count_digit)
                {
                    cerr << "Invalid index value: '";
                    cerr.write(s, n);
                    cerr << "'" << endl;
                    return false;
                }
            }

            uint32_t fragment = static_cast<uint32_t>(parsed_fragment);

            uint64_t read_fragment_key =
                (static_cast<uint64_t>(hit_read_id) << 32) |
                static_cast<uint64_t>(fragment);

            uint64_t count =
                (read_fragment_counts[read_fragment_key] += query_multiplicity * index_count);

            if (count > best_count)
            {
                best_count = count;
                best_read_id = hit_read_id;
                best_fragment = fragment;
            }

            if (p < n && s[p] == ',')
                ++p;
        }

        return true;
    };

    for (const QueryKmer& query_kmer : unique_query_kmers)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        dbkey_t kmer_key = make_kmer_index_key(query_kmer.encoded_kmer);

        char* kmer_ret = nullptr;
        access->get_entry_by_key.run(&access->get_entry_by_key, kmer_key, &kmer_ret);

        if (kmer_ret == nullptr)
            continue;

        if (kmer_ret[0] == '\0')
        {
            free(kmer_ret);
            continue;
        }

        const char* line_start = kmer_ret;

        for (const char* p = kmer_ret; ; ++p)
        {
            if (*p == '\n' || *p == '\0')
            {
                size_t line_len = static_cast<size_t>(p - line_start);

                if (line_len > 0 && line_start[line_len - 1] == '\r')
                    --line_len;

                if (line_len > 0)
                {
                    if (!process_index_value(line_start, line_len, query_kmer.multiplicity))
                    {
                        free(kmer_ret);
                        access->add_tag.run(&access->add_tag, filter_fail_tag);
                        return;
                    }
                }

                if (*p == '\0')
                    break;

                line_start = p + 1;
            }
        }

        free(kmer_ret);
    }

    if (best_count == 0)
    {
        access->add_tag.run(&access->add_tag, filter_done_tag);
        return;
    }

    double presence =
        static_cast<double>(best_count) /
        static_cast<double>(total_query_kmers);

    if (presence < THRESHOLD)
    {
        access->add_tag.run(&access->add_tag, filter_done_tag);
        return;
    }

    uint64_t position =
        static_cast<uint64_t>(best_fragment) *
        static_cast<uint64_t>(FRAG - OVERLAP);

    string candidate_value =
        query_read_id_str + "," +
        to_string(best_read_id) + "," +
        to_string(position) + "," +
        to_string(presence);

    const char* candidate_tags[] = {"candidate", ""};

    dbkey_t candidate_key = INITIAL_KEY;

    access->make_new_entry.run(
        &access->make_new_entry,
        candidate_tags,
        candidate_value.c_str(),
        candidate_key
    );

    access->add_tag.run(&access->add_tag, filter_done_tag);
}


bool should_run_overlap_search(const DBAccess* access, const char* filter_fail_tag, const char* filter_done_tag) {
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
    return overlap_search(access, filter_fail_tag, filter_done_tag);
}

// Fit the Pando API
extern "C" {
    /** @brief Main filter entry point */
    extern void run(void* access) {
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        return should_run_overlap_search(access, filter_fail_tag, filter_done_tag);
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

