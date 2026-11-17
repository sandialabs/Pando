#include <iostream>
#include <string>

#include "filter.hpp"
#include "dbkey.h"

using namespace std;
using namespace pando;

// Runs on entries tagged "Y". For an entry at key {a, b, c}, adds the tag
// "TagB" to the entry at {a+1, b, c} (by making a new entry with that key,
// which merges into the existing one). Used to test that a filter enabled on
// an entry the scan has already passed, possibly on another agent, still runs.

static const char* filter_name = "test_pardb_filters_tag_other";
static const char* filter_done_tag = "test_pardb_filters_tag_other:done";

void run_(const DBAccess *access) {
    dbkey_t target {access->key.a + 1, access->key.b, access->key.c};
    const char* new_tags[] = {"TagB", ""};
    access->make_new_entry.run(&access->make_new_entry, new_tags, "", target);
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
        bool y_found = false;
        while ((*tags)[0] != '\0') {
            if (string(*tags) == "Y")
                y_found = true;
            if (string(*tags) == filter_done_tag)
                return false;
            ++tags;
        }
        return y_found;
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
