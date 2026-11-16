#include <cstdlib>
#include <string>

#include "filter.hpp"
#include "dbkey.h"

using namespace std;
using namespace pando;

// Runs on the entry tagged "Q1" whose value is n. Makes n single-key
// get_entry_by_key calls for the keys {34, i, 0}, whose values are expected
// to be "v<i>", except that every 5th key has an empty value, which
// get_entry_by_key reports as missing (nullptr). Then makes n calls for
// keys {35, i, 0}, which do not exist. With two agents, about half the keys
// belong to the agent running the filter. Writes "ok=<correct>/<2n>" to
// entry {36, 0, 0}.

static const char* filter_name = "test_pardb_filters_get_entry_many";
static const char* filter_done_tag = "test_pardb_filters_get_entry_many:done";

void run_(const DBAccess *access) {
    size_t n = strtoul(access->value, nullptr, 10);
    size_t ok = 0;

    for (size_t i = 0; i < n; ++i) {
        char* r = nullptr;
        access->get_entry_by_key.run(&access->get_entry_by_key, dbkey_t{34, (vtx_t)i, 0}, &r);
        bool expect_missing = (i % 5 == 0);
        if (expect_missing ? r == nullptr : (r != nullptr && string(r) == "v" + to_string(i))) ++ok;
        free(r);
    }
    for (size_t i = 0; i < n; ++i) {
        char* r = nullptr;
        access->get_entry_by_key.run(&access->get_entry_by_key, dbkey_t{35, (vtx_t)i, 0}, &r);
        if (r == nullptr) ++ok;
        free(r);
    }

    string summary = "ok=" + to_string(ok) + "/" + to_string(2*n);
    const char* result_tags[] = {"result", ""};
    access->make_new_entry.run(&access->make_new_entry, result_tags, summary.c_str(), dbkey_t{36, 0, 0});
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
            if (string(*tags) == "Q1")
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
