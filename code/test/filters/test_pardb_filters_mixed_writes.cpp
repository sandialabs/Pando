#include <iostream>
#include <string>

#include "filter.hpp"
#include "dbkey.h"

using namespace std;
using namespace pando;

/* Makes every kind of write a filter can make, mostly to entries other agents
 * own, so that write batching (ParDB::write_batches_) is exercised for all of
 * them. Runs on entries tagged "W" with key {50, i, 0}:
 *  - creates {51, i, 0} tagged "made" with value "v<i>"
 *  - creates {52, 0, 0} tagged "shared" with value "<i>"; every run writes this
 *    key, so the values from all runs are concatenated
 *  - removes the tag "T" from {53, i, 0}
 *  - updates the value of {53, i, 0} twice, to "first" and then "u<i>"; the
 *    second must win, which needs the writes to that owner kept in order
 *  - subscribes {51, i, 1} to the creation of {51, i, 0}, releasing its tag
 *    "waiting". Both keys share (a, b), so they have the same owner.
 *  - tags its own entry done
 */

static const char* filter_name = "test_pardb_filters_mixed_writes";
static const char* filter_done_tag = "test_pardb_filters_mixed_writes:done";

void run_(const DBAccess *access) {
    vtx_t i = access->key.b;
    string idx = to_string(i);

    {
        const char* tags[] = {"made", ""};
        string value = "v" + idx;
        access->make_new_entry.run(&access->make_new_entry, tags, value.c_str(), dbkey_t{51, i, 0});
    }
    {
        const char* tags[] = {"shared", ""};
        access->make_new_entry.run(&access->make_new_entry, tags, idx.c_str(), dbkey_t{52, 0, 0});
    }

    dbkey_t target {53, i, 0};
    access->remove_tag.run(&access->remove_tag, target, "T");
    access->update_entry_val.run(&access->update_entry_val, target, "first");
    string final_val = "u" + idx;
    access->update_entry_val.run(&access->update_entry_val, target, final_val.c_str());

    access->subscribe_to_entry.run(&access->subscribe_to_entry, dbkey_t{51, i, 1}, dbkey_t{51, i, 0}, "waiting");

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
        bool w_found = false;
        while ((*tags)[0] != '\0') {
            if (string(*tags) == "W")
                w_found = true;
            if (string(*tags) == filter_done_tag)
                return false;
            ++tags;
        }
        return w_found;
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
