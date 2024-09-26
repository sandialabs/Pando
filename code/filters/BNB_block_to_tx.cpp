#include "ETH_BASED_block_to_tx.hpp"

/* ========================================================

   This is the tag of the actual blockchain, not the blockchain it was forked from.
   Edit this tag to the new crypto tag if creating filters for a new ETH fork.
   This is the only edit needed.

   ======================================================== */
#define CRYPTO_TAG "BNB"

#define FILTER "_block_to_tx"
#define FILTER_NAME CRYPTO_TAG FILTER

static const char* crypto_tag = CRYPTO_TAG;
static const char* filter_done_tag = FILTER_NAME ":done";
static const char* filter_fail_tag = FILTER_NAME ":fail";

void run_(const DBAccess *access) {
    return eth_based_block_to_tx(access, crypto_tag, filter_done_tag, filter_fail_tag);
}


// Fit the Pando API
extern "C" {
    /** @brief Main filter entry point */
    extern void run(void* access) {
        // Run internally
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        return should_run_eth_based_block_to_tx(access, crypto_tag, filter_done_tag, filter_fail_tag);
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

