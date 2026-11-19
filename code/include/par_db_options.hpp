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
};

}
