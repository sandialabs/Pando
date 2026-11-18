#include <iostream>
#include <string>

#include "filter.hpp"
#include "dbkey.h"

using namespace std;
using namespace pando;

/* Writes unusual values, to check that write batching (ParDB::write_batches_)
 * carries them exactly. Runs on entries tagged "E" with key {70, i, 0}:
 *  - creates {71, i, 0} tagged "big" whose value (big_value(i)) is larger
 *    than the default write batch flush size, so one write is bigger than a
 *    whole batch
 *  - creates {72, i, 0} tagged "empty" with an empty value
 *  - updates {73, i, 0} to an empty value
 *  - updates {74, i, 0} to a 200 KB value of 'z'
 *  - creates {75, i, 0} tagged "odd" with ODD_VALUE (separators, a newline,
 *    control and high bytes)
 *  - tags its own entry done
 * The filter API passes values as C strings, so a value cannot contain a NUL
 * byte. The test (test_par_db_filters.cpp) uses the same values.
 */

static const char* filter_name = "test_pardb_filters_edge_writes";
static const char* filter_done_tag = "test_pardb_filters_edge_writes:done";

#define ODD_VALUE "a,b|c:d\n\x01\x7f\xff end"

static string big_value(vtx_t i) {
    return string(300*1024 + i, (char)('a' + i % 26));
}

void run_(const DBAccess *access) {
    vtx_t i = access->key.b;

    {
        const char* tags[] = {"big", ""};
        string value = big_value(i);
        access->make_new_entry.run(&access->make_new_entry, tags, value.c_str(), dbkey_t{71, i, 0});
    }
    {
        const char* tags[] = {"empty", ""};
        access->make_new_entry.run(&access->make_new_entry, tags, "", dbkey_t{72, i, 0});
    }
    access->update_entry_val.run(&access->update_entry_val, dbkey_t{73, i, 0}, "");
    {
        string value(200*1024, 'z');
        access->update_entry_val.run(&access->update_entry_val, dbkey_t{74, i, 0}, value.c_str());
    }
    {
        const char* tags[] = {"odd", ""};
        access->make_new_entry.run(&access->make_new_entry, tags, ODD_VALUE, dbkey_t{75, i, 0});
    }

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
        bool e_found = false;
        while ((*tags)[0] != '\0') {
            if (string(*tags) == "E")
                e_found = true;
            if (string(*tags) == filter_done_tag)
                return false;
            ++tags;
        }
        return e_found;
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
