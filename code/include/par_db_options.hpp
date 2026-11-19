#pragma once

#include <cstddef>

#include "memory_policy.hpp"

namespace pando {

/** @brief Settings for a ParDB agent (see pando_pardb's command line)
 *
 * Collects the settings so that a new one does not need yet another
 * constructor argument. A value of 0 for a size means "use the default".
 */
struct ParDBOptions {
    /** @brief Skip processing of group filters */
    bool skip_group_filters = false;

    /** @brief Filter runs per processing pass before the stage closes (see
     * SeqDB::set_max_filters_per_stage); 0 = SeqDB's default (unlimited) */
    size_t max_filters_per_stage = 0;

    /** @brief When to end a processing pass early to bound memory (see
     * memory_policy.hpp); enabled by default, MemoryPolicy::disabled() turns
     * it off. Requests from other agents to close early are honored either
     * way, once a filter has run in the pass. An agents_per_node of 0 uses
     * Slurm's tasks per node, else the agents in the mesh with this agent's
     * IP address. */
    MemoryPolicy mem_policy;

    /** @brief Default flush size for write batches (see ParDB::set_write_batch_bytes) */
    static constexpr size_t DEFAULT_WRITE_BATCH_BYTES = 256 * 1024;

    /** @brief Flush size for write batches; 0 turns batching off */
    size_t write_batch_bytes = DEFAULT_WRITE_BATCH_BYTES;

    /** @brief Keys the filter scan fetches from the map per request (see
     * SeqDB::set_scan_block_keys); 0 = SeqDB's default */
    size_t scan_block_keys = 0;

    /** @brief Keep sweeping over the entries until a whole sweep runs no
     * filter (the default); false processes each entry once per request
     * (see SeqDB::set_continuous_processing) */
    bool continuous_processing = true;
};

}
