#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "filter.hpp"
#include "dbkey.h"

using namespace std;
using namespace pando;

// Runs on the entry tagged "Q" whose value is n. Looks up, in one batched
// call: the keys {30, i, 0} for i < n (whose values are expected to be i),
// a key that does not exist, a repeat of {30, 0, 0}, and {30, 5000, 0},
// whose value is expected to be empty. Writes a summary to entry {33, 0, 0}:
//   "ok=<matching values>/<n> missing=<0|1> dup=<0|1> empty=<0|1>"

static const char* filter_name = "test_pardb_filters_get_entries";
static const char* filter_done_tag = "test_pardb_filters_get_entries:done";

void run_(const DBAccess *access) {
    size_t n = strtoul(access->value, nullptr, 10);

    vector<dbkey_t> keys;
    for (size_t i = 0; i < n; ++i) keys.push_back({30, (vtx_t)i, 0});
    keys.push_back({30, (vtx_t)(n + 1000), 0});   // missing
    keys.push_back({30, 0, 0});                    // repeat
    keys.push_back({30, 5000, 0});                 // empty value

    vector<char*> values(keys.size(), nullptr);
    access->get_entries_by_keys.run(&access->get_entries_by_keys, keys.size(), keys.data(), values.data());

    size_t ok = 0;
    for (size_t i = 0; i < n; ++i)
        if (values[i] != nullptr && string(values[i]) == to_string(i)) ++ok;
    bool missing = values[n] == nullptr;
    bool dup = values[n+1] != nullptr && string(values[n+1]) == "0";
    bool empty = values[n+2] != nullptr && values[n+2][0] == '\0';

    for (char* v : values) free(v);

    string summary = "ok=" + to_string(ok) + "/" + to_string(n)
        + " missing=" + to_string(missing) + " dup=" + to_string(dup) + " empty=" + to_string(empty);
    const char* result_tags[] = {"result", ""};
    access->make_new_entry.run(&access->make_new_entry, result_tags, summary.c_str(), dbkey_t{33, 0, 0});
    access->add_tag.run(&access->add_tag, filter_done_tag);
}

// Fit the Pando API
extern "C" {
    /** @brief Main filter entry point */
    extern void run(void* access) {
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        auto tags = access->tags;
        bool q_found = false;
        while ((*tags)[0] != '\0') {
            if (string(*tags) == "Q")
                q_found = true;
            if (string(*tags) == filter_done_tag)
                return false;
            ++tags;
        }
        return q_found;
    }

    /** @brief Contains the entry point and tags for the filter */
    extern const FilterInterface filter {
        filter_name: filter_name,
        filter_type: SINGLE_ENTRY,
        should_run: &should_run,
        init: nullptr,
        destroy: nullptr,
        run: &run
    };
}
