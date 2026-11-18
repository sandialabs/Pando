#include <chrono>
#include <string>
#include <thread>

#include "filter.hpp"
#include "dbkey.h"

using namespace std;
using namespace pando;

// Runs once on each entry tagged "S", taking a couple of milliseconds, so a
// pass over many such entries lasts long enough for a request from another
// agent (CLOSE_EARLY) to arrive while it runs.

static const char* filter_name = "test_pardb_filters_slow_tag";
static const char* filter_done_tag = "test_pardb_filters_slow_tag:done";

void run_(const DBAccess *access) {
    this_thread::sleep_for(chrono::milliseconds(2));
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
        bool s_found = false;
        while ((*tags)[0] != '\0') {
            if (string(*tags) == "S")
                s_found = true;
            if (string(*tags) == filter_done_tag)
                return false;
            ++tags;
        }
        return s_found;
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
