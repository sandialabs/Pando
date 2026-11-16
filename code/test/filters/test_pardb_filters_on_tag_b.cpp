#include <iostream>
#include <string>

#include "filter.hpp"
#include "dbkey.h"

using namespace std;
using namespace pando;

// Runs once on each entry tagged "TagB" (see test_pardb_filters_tag_other)

static const char* filter_name = "test_pardb_filters_on_tag_b";
static const char* filter_done_tag = "test_pardb_filters_on_tag_b:done";

void run_(const DBAccess *access) {
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
        bool b_found = false;
        while ((*tags)[0] != '\0') {
            if (string(*tags) == "TagB")
                b_found = true;
            if (string(*tags) == filter_done_tag)
                return false;
            ++tags;
        }
        return b_found;
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
