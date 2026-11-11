#include <iostream>

#include "filter.hpp"
#include "dbkey.h"
#include <algorithm>
#include <boost/algorithm/string.hpp>

#include <sstream>
#include <math.h>

#include <string>

using namespace std;
using namespace pando;

#define FILTER_NAME "product_engagement_90"
static const char* filter_name = FILTER_NAME;
static const char* filter_done_tag = FILTER_NAME ":done";
static const char* filter_fail_tag = FILTER_NAME ":fail";
static const char* filter_inactive_tag = FILTER_NAME ":inactive";

template<typename ...Args>
void fail_(const DBAccess *access, const char* filter_fail_tag, const char* filter_name, Args && ...args) {
    cerr << "[ERROR] unable to run filter: " << filter_name;
    (cerr << " " << ... << args);
    cerr << endl;
    access->add_tag.run(&access->add_tag, filter_fail_tag);
}

void run_(const DBAccess *access) {
    string val_s = access->value;
    // extract the fields into a vector
    std::vector<std::string> fields;
    boost::split(fields, val_s, boost::is_any_of(","));

    if (fields.size() != 3) 
    {
        return fail_(access, filter_fail_tag, filter_name, "String split was the wrong size");
    }

    const size_t X = 11; // the quality score must be at least X to join, will vary for each experiment

    size_t engagement_score = stoul(fields[2]);
    if (engagement_score < X) {
        access->add_tag.run(&access->add_tag, filter_done_tag);
        return;
    }

    vtx_t product_id = stoul(fields[1]);
    chain_info_t chain_info = pack_chain_info(PRODUCT_ENGAGEMENT_KEY, PRODUCT_ID_KEY, 0);
    dbkey_t search_key {chain_info, product_id, 0};

    // lookup the product ID
    char* queried_entry = nullptr;
    access->get_entry_by_key.run(&access->get_entry_by_key, search_key, &queried_entry);
    string queried_entry_s;
    if (queried_entry != nullptr) {
        queried_entry_s.assign(queried_entry);
        free(queried_entry);
    } else {
        access->add_tag.run(&access->add_tag, filter_inactive_tag);
        access->subscribe_to_entry.run(&access->subscribe_to_entry, access->key, search_key, filter_inactive_tag);
        return;
    }

    std::vector<std::string> queried_fields;
    boost::split(queried_fields, queried_entry_s, boost::is_any_of(","));
    if (queried_fields.size() != 2) {
        return fail_(access, filter_fail_tag, filter_name, "Queried string split was the wrong size");
    }

    string joined = val_s + "," + queried_fields[1];
    const char* new_tags[] = {"product_engagement_join", ""};

    access->make_new_entry.run(&access->make_new_entry, new_tags, joined.c_str(), INITIAL_KEY);
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
        bool found_table1_tag = false;

        while ((*tags)[0] != '\0') {
            if (string(*tags) == "product_engagement_table1")
                found_table1_tag = true;
            if (string(*tags) == filter_done_tag)
                return false;
            if (string(*tags) == filter_fail_tag)
                return false;
            if (string(*tags) == filter_inactive_tag)
                return false;
            ++tags;
        }
        return found_table1_tag;
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

