#include "ETH_BASED_tx_vals.hpp"

/* ========================================================

   This is the tag of the actual blockchain, not the blockchain it was forked from.
   Edit this tag to the new crypto tag if creating filters for a new ETH fork.
   This is the only edit needed.

   ======================================================== */
#define CRYPTO_TAG "ETH"

#define FILTER "_tx_vals"
#define FILTER_NAME CRYPTO_TAG FILTER

static const char* crypto_tag = CRYPTO_TAG;
static const char* filter_name = FILTER_NAME;
static const char* filter_done_tag = FILTER_NAME ":done";
static const char* filter_fail_tag = FILTER_NAME ":fail";
static const char* inactive_tag = FILTER_NAME ":inactive";

void run_(const DBAccess *access) {
    return eth_based_tx_vals(access, crypto_tag, filter_name, filter_done_tag, filter_fail_tag, inactive_tag);
}

// Fit the Pando API
extern "C" {
    /** @brief Main filter entry point */
    extern void run(void* access) {
        // Run internally
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        return should_run_eth_based_tx_vals(access, crypto_tag, filter_done_tag, filter_fail_tag, inactive_tag); 
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

