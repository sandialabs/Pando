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

        // The entries behind every group edge. Each edge's DBAccess points
        // into one of these (its tags and value are not copied), so they must
        // stay alive, and must not move, until teardown_graph() deletes those
        // accesses. A list never relocates its elements as it grows.
        list<DBEntry<>> group_entries_;

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

            group_entries_.clear();

            // Only the keys and tags of each edge entry are loaded, never the
            // values, so the whole DB is not copied into memory. Edge accesses
            // therefore have value == nullptr; a group filter that needs an
            // edge's value must look it up with get_entry_by_key. Entries are
            // fetched in batches to keep each request/response small.
            vector<dbkey_t> edge_keys;
            if (!skip_group_filters_) {
                for (auto & k : db_.keys()) {
                    // Random keys are not part of any group
                    if (!is_random_key(k))
                        edge_keys.push_back(k);
                }
            }

            const size_t BATCH_SIZE = 1ull<<16;
            for (size_t begin = 0; begin < edge_keys.size(); begin += BATCH_SIZE) {
                for (auto & entry : db_.retrieve_tags_by_keys(edge_keys, begin, BATCH_SIZE)) {
                    dbkey_t k = entry.get_key();

                    group_entries_.push_back(move(entry));

                    DBAccess* acc = group_entries_.back().access();
                    acc->value = nullptr;
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
        /** @brief Run one iteration (superstep) of the group filters
         *
         * Vertex-centric (Pregel) semantics:
         *  - a vertex runs if it is ACTIVE or has messages for this iteration;
         *    a message wakes up a vertex that voted to halt (INACTIVE);
         *  - every vertex sees the global iteration in group->iter, whether or
         *    not it ran in earlier iterations;
         *  - the computation continues while any vertex is still ACTIVE or any
         *    message was sent for the next iteration, so a message sent in the
         *    same iteration a vertex halts is still delivered.
         *
         * Messages a filter sends go to out_msgs[iter+1]. Messages from earlier
         * iterations are dropped here once they have been consumed or
         * delivered.
         *
         * @return whether another iteration is needed
         */
        bool compute_internal() {
            // Drop consumed incoming messages and already-delivered outgoing ones
            for (auto & [graph_key, msgs] : in_graph_msgs)
                for (auto it = msgs.begin(); it != msgs.end();)
                    it = (it->first < iter_) ? msgs.erase(it) : next(it);
            for (auto & [graph_key, msgs] : out_graph_msgs)
                for (auto it = msgs.begin(); it != msgs.end();)
                    it = (it->first <= iter_) ? msgs.erase(it) : next(it);

            size_t active = 0;

            for (auto & [graph_key, graph] : filter_states_) {
                // Messages delivered to this graph for the current iteration
                const itmsg_t* msgs_now = nullptr;
                auto in_graph = in_graph_msgs.find(graph_key);
                if (in_graph != in_graph_msgs.end()) {
                    auto at_iter = in_graph->second.find(iter_);
                    if (at_iter != in_graph->second.end())
                        msgs_now = &(at_iter->second);
                }

                for (auto & [v, fs_pair] : graph) {
                    auto & [acc, filter_state] = fs_pair;
                    acc->group->iter = iter_;

                    bool has_msgs = false;
                    if (msgs_now != nullptr) {
                        auto vtx_msgs = msgs_now->find(v);
                        has_msgs = vtx_msgs != msgs_now->end() && !vtx_msgs->second.empty();
                    }

                    // Halted vertices are skipped unless a message wakes them
                    if (acc->group->state != ACTIVE && !has_msgs)
                        continue;
                    acc->group->state = ACTIVE;

                    bool ran = false;
                    for (size_t idx = 0; idx < filters_.size(); ++idx) {
                        if (filter_state[idx] != nullptr) {
                            filters_[idx]->run_with_state(filter_state[idx]);
                            ran = true;
                        }
                    }

                    if (ran && acc->group->state == ACTIVE)
                        ++active;
                }
            }

            // Any message sent for the next iteration keeps the computation going
            bool pending = false;
            for (auto & [graph_key, msgs] : out_graph_msgs) {
                auto next_iter = msgs.find(iter_ + 1);
                if (next_iter == msgs.end()) continue;
                for (auto & [vtx, vtx_msgs] : next_iter->second) {
                    if (!vtx_msgs.empty()) { pending = true; break; }
                }
                if (pending) break;
            }

            ++iter_;
            return active > 0 || pending;
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
            // Only now that the edge accesses are deleted can their entries go
            group_entries_.clear();
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
                // Deliver the messages sent for the next iteration (iter_ now
                // names it); older ones are dropped by compute_internal()
                for (auto & [graph, msgs] : out_graph_msgs) {
                    auto next_iter = msgs.find(iter_);
                    if (next_iter != msgs.end())
                        in_graph_msgs[graph][iter_] = move(next_iter->second);
                }
            }

            // Now computation is finished, make writes to DB etc.
            stage_close();

            teardown_graph();

            return false;
        }
};

}
