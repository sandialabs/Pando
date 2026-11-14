#pragma once

#include "alg_access.hpp"
#include "seq_db.hpp"
#include "terr.hpp"

#include <unordered_map>
#include <type_traits>
#include <memory>
#include <string>
#include <vector>
#include <list>
#include <set>

using namespace std;

namespace pando {

/** @brief Contains an algorithm-processing DB
 *
 * This is built on top of the seq DB
 */
class AlgDB : public SeqDB {
    private:
        // Find all groups based on keys
        unordered_map<graph_t, unordered_map<vtx_t, GraphEntry>> graphs_;

        set<tuple<graph_t, vtx_t>> vertices_;

        // Keep track of the filter states within the graph
        unordered_map<graph_t, unordered_map<vtx_t, pair<DBAccess*, vector<void*>>>> filter_states_;

        // Find all filters that run on entry groups
        vector<filter_p> filters_;

        itkey_t iter_ = 0;

        bool skip_group_filters_ = false;

    public:
        using SeqDB::SeqDB;

        itkey_t get_iter() const { return iter_; }

        void disable_group_filters() {
            skip_group_filters_ = true;
        }

        /** @brief Whether any installed filter runs on entry groups
         *
         * Without one there is nothing to compute, so the group phase can be
         * skipped entirely (no vertex setup, no copy of the DB). */
        bool has_group_filters() const {
            for (const auto & [filter_name, filter] : installed_filters_) {
                if (filter->filter_type() == GROUP_ENTRIES)
                    return true;
            }
            return false;
        }

        //                                 SEND it, DEST vtx,  data..
        //graph key (graph_t), then msg_t = <itkey_t, <vertex_t, vector<MData>>>
        unordered_map<graph_t, msg_t> in_graph_msgs;
        unordered_map<graph_t, msg_t> out_graph_msgs;

        void setup_graph_datastructures() {
            // Sequential version
            // Setup 'vertices_' for myself. Only the keys are needed here, so
            // read keys rather than copying every entry (the entries
            // themselves are read once, in setup_graph_datastructures_have_ids)
            vector<dbkey_t> ret_keys;
            if (!skip_group_filters_) {
                ret_keys = db_.keys();
            }
            for (auto & k : ret_keys) {
                // Random keys (e.g. entries created with INITIAL_KEY) are not
                // part of any group
                if (is_random_key(k)) continue;
                add_alg_vertex({k.a, k.b});
                add_alg_vertex({k.a, k.c});
            }
            
            // Call next step
            setup_graph_datastructures_have_ids();
        }

        void add_alg_vertex(tuple<graph_t, vtx_t> vtx) {
            vertices_.insert(vtx);
        }

        void setup_graph_datastructures_have_ids() {
            // Only proceed to this state if we get all vertex IDs from all other neighbors
            // These IDs will be in the variable vertices_

            for (auto & [filter_name, filter] : installed_filters_) {
                if (filter->filter_type() == GROUP_ENTRIES) {
                    filters_.push_back(filter);
                }
            }

            for (auto & [graph, vtx] : vertices_) {
                // Create vertices
                graphs_[graph][vtx];
            }

            list<DBEntry<>> entries;

            vector<DBEntry<>> ret_entries;
            if (!skip_group_filters_) {
                ret_entries = db_.retrieve_all_entries();
            }
            for (auto & entry : ret_entries) {         // TODO : replace this with a C++ iterator that automatically "batches" behind the scene
                if (entry.random_key()) continue;
                dbkey_t k = entry.get_key();

                entries.push_back(move(entry));

                DBAccess* acc = entries.back().access();
                add_db_access(acc);

                // Populate the graph
                // We know we have [k.b] - by hash constraint on what is in our DB
                // assume k.a is something fixed
                // we have all db entries:
                //  OUT [k.b][a1]
                //  OUT [k.b][a2]
                //  OUT [k.b][..]
                //  OUT [k.b][aN]
                //
                //  \        /
                //   --------- ---> due to hashing with k.c = 0
                //
                // we are the correct processor for k.b ✓

                graphs_[k.a][k.b].out.emplace_back(k.c, acc);
            }

            // Now, iterate through the graph and run on each entry group
            for (auto & [key, graph] : graphs_) {
                for (auto & [v, entry] : graph) {
                    // Add in the src and neighbor list, along with other algorithm objects
                    GroupAccess* gaccess = new GroupAccess { v, entry, &(in_graph_msgs[key]), &(out_graph_msgs[key])};
                            // see https://stackoverflow.com/questions/62253972/is-it-safe-to-reference-a-value-in-unordered-map for safety of reference to value in unordered_map
                    DBAccess* access = new DBAccess {};
                    access->key.a = key;
                    access->key.b = v;
                    access->group = gaccess;
                    add_db_access(access);

                    // Add the filter
                    vector<void*> fs_vec;
                    fs_vec.reserve(filters_.size());
                    for (auto & filter : filters_) {
                        if (filter->should_run(access)) {
                            fs_vec.push_back(filter->init(access));
                        } else {
                            fs_vec.push_back(nullptr);
                        }
                    }
                    filter_states_[key][v] = {access, fs_vec};
                }
            }

        }
        bool compute_internal() {
            int cont = 0;
            // Note: this will terminate when cont is never set to true,
            // which happens if all group state values are != ACTIVE after
            // running
            // If just 1 is ACTIVE, the loop will continue and execute
            // every filter for every vertex again

            // Begin the processing loop
            for (auto & [graph_key, graph] : filter_states_) {
                for (auto & [v, fs_pair] : graph) {
                    auto & [acc, filter_state] = fs_pair;

                    // Always run, and keep track of whether there is some
                    // ACTIVE output
                    for (size_t idx = 0; idx < filters_.size(); ++idx) {
                        if (filter_state[idx] != nullptr) {
                            filters_[idx]->run_with_state(filter_state[idx]);
                            if (acc->group->state == ACTIVE)
                                ++cont;
                        }
                    }

                    ++acc->group->iter;
                }
            }
            ++iter_;
            return cont > 0;
        }
        void teardown_graph() {
            // Clear messages
            in_graph_msgs.clear();
            out_graph_msgs.clear();

            // Reset iteration tracker
            iter_ = 0;

            // Finally, cleanup the state for each filter
            for (auto & [key, graph] : filter_states_) {
                for (auto & [v, fs] : graph) {
                    auto [acc, filter_state] = fs;
                    for (size_t idx = 0; idx < filters_.size(); ++idx) {
                        if (filter_state[idx] != nullptr)
                            filters_[idx]->destroy(filter_state[idx]);
                    }
                    delete acc->group;
                    delete acc;
                }
            }

            // Clean up the graph
            for (auto & [key, graph] : graphs_) {
                for (auto & [v, entry] : graph) {
                    // Only delete the in direction, as one DB access is used for
                    // both sides (in and out)
                    for (auto & edge : entry.out)
                        delete edge.acc;
                }
            }

            graphs_.clear();
            vertices_.clear();
            filter_states_.clear();
            filters_.clear();
        }

        // 4 parts:
        // 1) setup graph datastructures
        // 2) inside while loop compute
        // 3) should_run while loop(?)
        // 4) teardown
        //
        // In non-parallel version:
        //  run 1, while (3) { 2 }, 4
        //
        // In parallel version:
        //  run 1
        //  go into ParDB participant loop (state change here?)
        //    one new state would be: run 2 then change state to run 3
        //    another new state would be: run 3, then change state if appropriate
        //    another state would be run 4

        bool seq_process() {
            return SeqDB::process();
        }

        /** @brief Process filters */
        virtual bool process() {
            // This is sequential only

            // First, process all SINGLE_ENTRY filters
            seq_process();

            // Nothing more to do without group filters; skip building the
            // graph, which would otherwise read every entry in the DB
            if (skip_group_filters_ || !has_group_filters())
                return false;

            // Next, process GROUP_ENTRY filters
            setup_graph_datastructures();

            // @TODO Right now, the state and message requirement checking is
            // per group. This is limiting as all filters must abide by / use
            // the same state. Refactor to make these all <group,filter>
            // specific

            while (compute_internal()) {
                // Move out to in msgs
                for (auto & [graph, msg] : out_graph_msgs) {
                    in_graph_msgs[graph] = msg;
                }
            }

            // Now computation is finished, make writes to DB etc.
            stage_close();

            teardown_graph();

            return false;
        }
};

}
