#pragma once

#include <unordered_map>
#include <filesystem>
#include <exception>
#include <stdexcept>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <charconv>
#include <cmath>
#include <functional>
#include <string_view>
#include <system_error>
#include "absl/container/flat_hash_map.h"
#include "Python.h"

#include "pigo.hpp"

#include "filter.hpp"
#include "dbentry.hpp"
#include "random_key_gen.hpp"
#include "pando_map.hpp"
#include "pando_map_client.hpp"
#include "par_db_thread.hpp"
#include "terr.hpp"
#include "process_stats.hpp"
#include "memory_policy.hpp"

#ifdef __GLIBC__
#include <malloc.h>
#endif

using namespace std;
using namespace std::chrono;

extern "C" {
static void s_SeqDB_new_entry(const fn* s_this, ...);
static void s_SeqDB_get_entry_by_tags(const fn* s_this, ...);
static void s_SeqDB_get_entry_by_key(const fn* s_this, ...);
static void s_SeqDB_get_entries_by_keys(const fn* s_this, ...);
static void s_SeqDB_get_entries_by_tags(const fn* s_this, ...);
static void s_SeqDB_add_tag(const fn* s_this, ...);
static void s_SeqDB_remove_tag(const fn* s_this, ...);
static void s_SeqDB_subscribe_to_entry(const fn* s_this, ...);
static void s_SeqDB_update_entry_val(const fn* s_this, ...);
}

typedef int (*db_query_t)(dbkey_t, const char*, const char* const*, const DBAccess*);

namespace pando {

static string MERGE_STRATEGY_FORCE = "MERGE_STRATEGY=FORCE_MERGE";
/** Entries with the same key that both carry this tag have their numeric
 * values added when merged (see SeqDB::sum_entries) */
static string MERGE_STRATEGY_SUM = "MERGE_STRATEGY=SUM";
static string MERGED_SUM = "MERGED_SUM";
static string MERGED_SUM_FAILED = "MERGED_SUM_FAILED";

/** @brief Start the embedded Python interpreter once per process
 *
 * Cython filters use single-phase init and keep their module object in a
 * static, so they cannot be re-imported into a new interpreter after
 * Py_FinalizeEx(). The interpreter therefore lives for the whole process
 * (shared by every SeqDB) and is finalized once at exit.
 */
inline void ensure_python_initialized() {
    if (!Py_IsInitialized()) {
        Py_Initialize();
        std::atexit([]() {
            if (Py_IsInitialized())
                Py_FinalizeEx();
        });
    }
}

/** @brief Contains the sequential database
 *
 * This is a very simple database that runs filters.
 */
class SeqDB {
    private:
        /** @brief Hold filters by name */
        absl::flat_hash_map<string, filter_p> filters_;

        /** @brief Index entries by tags for easy access*/
        absl::flat_hash_map<string, set<dbkey_t>> entries_by_tag_;

        /** @brief Return a set of indices that correspond to a DBEntry's index in db_ */
        set<dbkey_t> get_entry_by_tags_(const char* const* c_tags) {
            vector<string> tags = Filter::extract_c_tags(c_tags);

            set<dbkey_t> matching_entries;
            set<dbkey_t> empty_set;

            if (tags.size() == 0)
                return matching_entries;

            //(entry IDs are integer)
            bool first = true;
            for (auto & tag : tags) {
                // Check if the tag exists in the DB; if not, return
                // (Intersection with an empty set is empty)
                if (entries_by_tag_.count(tag) == 0) return empty_set;

                // Treat the first tag specially
                if (first) {
                    matching_entries = entries_by_tag_[tag];
                    first = false;
                } else {
                    set<dbkey_t> new_entries;
                    set_intersection(matching_entries.begin(), matching_entries.end(),
                        entries_by_tag_[tag].begin(), entries_by_tag_[tag].end(),
                        inserter(new_entries, new_entries.begin()));
                    matching_entries.swap(new_entries);
                }
            }
            return matching_entries;
        }

        /** @brief Add a new entry by tag */
        void add_to_tag_index_(dbkey_t k, string tag) {
            entries_by_tag_[tag].insert(k);
        }
        template<typename ...Args>
        void add_to_tag_index_(dbkey_t k, string tag, Args && ...args) {
            add_to_tag_index_(k, tag);
            add_to_tag_index_(k, args...);
        }

        /** @brief Remove an entry by tag */
        void remove_from_tag_index_(dbkey_t k, string tag) {
            entries_by_tag_[tag].erase(k);
        }
        template<typename ...Args>
        void remove_from_tag_index_(dbkey_t k, string tag, Args && ...args) {
            remove_from_tag_index_(k, tag);
            remove_from_tag_index_(k, args...);
        }

        /** @brief Store a map of entries that are waiting for other entries to
         * be created. The first dbkey_t is the key of the entry that is being
         * waited for. The second dbkey_t is the key of the entry that is
         * waiting. The string is the tag to be removed once that entry has
         * been created.
         */
        absl::flat_hash_map<dbkey_t, vector<pair<dbkey_t, string>>> entry_subscriptions_;

        /** @brief An agent id/address to use as part of the random seed. This
         * is intended to be set by the par_db. If not running in parallel
         * (e.g. for testing), simply default to 0
         */
        uint64_t agent_id_ = 0;

        RandomKeyGen random_key_gen_;

        void initialize_random_key_gen_() {
            random_key_gen_.initialize_seed(agent_id_);
        }

    protected:
        /** @brief State of the simple parser */
        enum parse_state_ {
            NOENTRY,
            TAGS,
            VALUE
        };

        /** @brief Tags to add to entries */
        vector<pair<dbkey_t, string>> tags_to_add_;

        /** @brief Tags to remove from entries */
        vector<pair<dbkey_t, string>> tags_to_remove_;

        /** @brief Hold entries to add in bulk */
        absl::flat_hash_map<dbkey_t, DBEntry<>> new_entries_;

        /** @brief Hold the actual database */
        ParDBThread<PandoMap> db_server_;
        PandoMapClient db_;

        /** @brief Hold the install filters */
        absl::flat_hash_map<string, filter_p> installed_filters_;

        /** @brief Hold entries that need their values updates */
        absl::flat_hash_map<dbkey_t, string> val_updates_;

        /** @brief How many filter runs one pass of run_filters_() may do
         * before it stops, so the stage can close. Entries created by those
         * runs (and messages for them) are held in memory until the stage
         * closes, so a smaller value bounds that memory at the cost of more
         * passes. Defaults to UNLIMITED_FILTERS_PER_STAGE, i.e. a pass only
         * stops at the end of its snapshot of the keys.
         */
        size_t max_filters_per_stage_ = UNLIMITED_FILTERS_PER_STAGE;

        /** @brief Cursor over the entries for run_filters_(): a snapshot of
         * the keys and the next position in it. A pass that stops at
         * max_filters_per_stage_ leaves the cursor where it stopped, so the
         * next pass continues there instead of rescanning from the first key.
         * scan_pos_ == 0 means the next pass starts a new lap on a fresh
         * snapshot.
         */
        vector<dbkey_t> scan_keys_;
        size_t scan_pos_ = 0;

        /** @brief Total entries offered to the filters (for tests/diagnostics) */
        size_t entries_examined_ = 0;

        /** @brief Entries whose values the scan fetched, and the requests it
         * took (for tests/diagnostics) */
        size_t scan_values_fetched_ = 0;
        size_t scan_value_requests_ = 0;

        /** @brief Continuous processing (see set_continuous_processing): when
         * set (the default), a process request keeps sweeping over the
         * entries until a whole sweep runs no filter; when not, it makes one
         * sweep and stops */
        bool continuous_processing_ = true;

        /** @brief With continuous processing off, the current request's sweep
         * is finished: later passes of the same request do nothing */
        bool sweep_done_ = false;

        /** @brief When to end a pass early to bound memory; enabled by
         * default (see memory_policy.hpp and set_memory_policy) */
        MemoryPolicy mem_policy_;

        /** @brief Where the system memory is read from (replaceable for tests) */
        function<MemInfo()> meminfo_source_ = []() { return read_meminfo(); };

        /** @brief Works out the agents sharing the node when the policy's
         * agents_per_node is 0 (auto); unset means 1 */
        function<uint64_t()> agents_per_node_source_;

        /** @brief Asked (at most every check_interval) whether another agent
         * wants this pass to end. The argument says whether a filter has run
         * in this pass yet; see set_stop_requested_callback. */
        function<bool(bool)> stop_requested_cb_;

        /** @brief Filter runs in the current (or, between passes, the last)
         * run_filters_() */
        size_t filters_run_last_pass_ = 0;

        /** @brief This pass checks MemAvailable against mem_reserve_bytes_ */
        bool mem_pressure_check_ = false;
        uint64_t mem_reserve_bytes_ = 0;
        steady_clock::time_point next_stop_check_;
        bool meminfo_warned_ = false;

        /** @brief Set up the stop checks for a new pass: reset the byte count
         * and, if the policy is enabled, compute this pass's budget from the
         * memory headroom now */
        void begin_stop_checks_() {
            stats_.stop_reason = STOP_NONE;
            stats_.bytes_emitted = 0;
            stats_.byte_budget = NO_BYTE_BUDGET;
            mem_pressure_check_ = false;
            mem_reserve_bytes_ = 0;
            // The first timed check happens right after the pass's first entry
            next_stop_check_ = steady_clock::now();

            if (!mem_policy_.enabled()) return;

            uint64_t budget = mem_policy_.pass_budget_bytes > 0 ? mem_policy_.pass_budget_bytes : NO_BYTE_BUDGET;
            if (mem_policy_.limit_pct > 0) {
                MemInfo mem = meminfo_source_();
                if (mem.ok) {
                    PassBudget b = compute_pass_budget(mem_policy_, mem, agents_per_node());
                    mem_pressure_check_ = true;
                    mem_reserve_bytes_ = b.reserve_bytes;
                    budget = min(budget, b.budget_bytes);
                } else if (!meminfo_warned_) {
                    meminfo_warned_ = true;
                    cerr << "[Pando] [WARNING] unable to read system memory; the memory limit is not applied"
                         << (mem_policy_.pass_budget_bytes > 0 ? " (the fixed pass budget still is)" : "") << endl;
                }
            }
            stats_.byte_budget = budget;
        }

        /** @brief Decide whether the pass should stop now
         *
         * Called after each entry. max_filters_per_stage_ applies as always.
         * The memory checks only apply once at least one filter has run this
         * pass. That is what guarantees progress however high memory is when
         * the pass starts: a budget of 0 would otherwise stop a pass before it
         * did anything, and since an unfinished lap must report "process
         * again", passes would repeat forever without doing any work.
         *
         * Requests from other agents are asked about before any run too, and
         * stop_requested_cb_ is told whether a filter has run. It may end a
         * pass that has run nothing here only if the pass made progress
         * elsewhere (ParDB: the asking agent ran a filter in it; see
         * CloseEarlyState). The pass still scanned at least one entry, and the
         * cursor carries over, so no scanning is lost.
         *
         * The byte budget is checked every time (it is just a comparison);
         * reading the system memory and asking stop_requested_cb_ are rate
         * limited by check_interval.
         *
         * @return a stop_reason_t, STOP_NONE to keep going
         */
        uint8_t check_stop_(size_t num_filters_run) {
            if (num_filters_run >= max_filters_per_stage_) return STOP_MAX_FILTERS;
            bool have_run = num_filters_run > 0;
            if (have_run && stats_.bytes_emitted >= stats_.byte_budget) return STOP_MEM_BUDGET;
            bool pressure_check = have_run && mem_pressure_check_;
            if (!pressure_check && !stop_requested_cb_) return STOP_NONE;

            auto now = steady_clock::now();
            if (now < next_stop_check_) return STOP_NONE;
            next_stop_check_ = now + mem_policy_.check_interval;

            if (pressure_check) {
                MemInfo mem = meminfo_source_();
                if (mem.ok && mem.available_bytes < mem_reserve_bytes_) return STOP_MEM_PRESSURE;
            }
            if (stop_requested_cb_ && stop_requested_cb_(have_run)) return STOP_REMOTE;
            return STOP_NONE;
        }

        /** @brief The scan fetches the keys and tags (not values) of the
         * entries from the map in blocks of this many keys, one request per
         * block (see set_scan_block_keys) */
        size_t scan_block_keys_ = DEFAULT_SCAN_BLOCK_KEYS;
        /** @brief The scan's state for the current window of a block: which
         * filters should run on each entry of the window, and the values
         * fetched for the entries that need them
         *
         * should_run only sees an entry's tags (its value is nullptr), so
         * nothing is fetched for an entry no filter runs on. In a pass that
         * runs little, that skips copying nearly every value out of the map.
         * Asking should_run ahead of earlier entries' runs gives the same
         * answers as asking in order: the runs only stage their changes,
         * which are not visible until the stage closes.
         */
        struct ScanLookahead {
            size_t begin = 0, end = 0;      // block indices of the window
            vector<uint8_t> wants;          // [(i - begin) * filters + f]: filter f should run on entry i
            vector<dbkey_t> need_keys;      // keys of the window's entries some filter runs on, in order
            size_t need_used = 0;           // need_keys handed out so far
            size_t fetched_end = 0;         // need_keys [.., fetched_end) are covered by vals
            vector<DBEntry<>> vals;         // the whole entries for the latest fetch, in key order
            size_t vals_next = 0;           // next of vals to hand out
        };

        /** @brief Ask should_run of every filter about the entries of block
         * from index from on (up to SCAN_LOOKAHEAD_ENTRIES of them), starting
         * a new window in la */
        void scan_decide_window_(vector<DBEntry<>>& block, size_t from,
                                 const vector<pair<Filter*, FilterCounters*>>& filters, ScanLookahead& la) {
            size_t nf = filters.size();
            la.begin = from;
            la.end = min(from + SCAN_LOOKAHEAD_ENTRIES, block.size());
            la.wants.assign((la.end - la.begin) * nf, 0);
            la.need_keys.clear();
            la.need_used = 0;
            la.fetched_end = 0;
            la.vals.clear();
            la.vals_next = 0;

            if (nf == 0) return;
            for (size_t i = la.begin; i < la.end; ++i) {
                // One access serves every filter's should_run: should_run
                // cannot change the entry (any change it makes is staged)
                unique_ptr<DBAccess> access { block[i].access() };
                access->value = nullptr;
                add_db_access(access.get());
                bool any = false;
                for (size_t f = 0; f < nf; ++f) {
                    if (filters[f].first->should_run(access.get())) {
                        la.wants[(i - la.begin) * nf + f] = 1;
                        any = true;
                    }
                }
                if (any) la.need_keys.push_back(block[i].get_key());
            }
        }

        /** @brief The whole entry (with its value) for the next entry of the
         * window that a filter runs on, fetching the values for it and the
         * window's later such entries if they are not here yet
         * @return nullptr if the entry no longer exists */
        DBEntry<>* scan_next_value_(ScanLookahead& la) {
            size_t i = la.need_used++;
            if (i >= la.need_keys.size()) throw runtime_error("scan: more entries need values than were asked about");
            dbkey_t key = la.need_keys[i];
            if (i >= la.fetched_end) {
                // The reply is capped at about SCAN_BLOCK_BYTES, so it may
                // cover only some of the rest (always at least this one)
                size_t covered = 0;
                la.vals = db_.retrieve_entries_by_keys(la.need_keys, i, la.need_keys.size() - i,
                                                       SCAN_BLOCK_BYTES, covered);
                la.vals_next = 0;
                la.fetched_end = i + covered;
                scan_values_fetched_ += la.vals.size();
                ++scan_value_requests_;
            }
            // The reply skips keys that no longer exist
            if (la.vals_next < la.vals.size() && la.vals[la.vals_next].get_key() == key)
                return &la.vals[la.vals_next++];
            return nullptr;
        }

        /** @brief Start the next pass on a fresh snapshot, freeing the old one */
        void reset_scan_() {
            vector<dbkey_t>().swap(scan_keys_);
            scan_pos_ = 0;
        }

    protected:
        /** @brief Processing statistics, see process_stats.hpp */
        ProcessStats stats_;

        /** @brief Counters of the filter currently running, so that its
         * outputs (new entries, tags, ...) are attributed to it */
        FilterCounters* cur_filter_ = nullptr;

        /** @brief Sets cur_filter_ for a scope, clearing it even on throw */
        struct CurrentFilter {
            FilterCounters*& slot;
            CurrentFilter(FilterCounters*& s, FilterCounters* c) : slot(s) { slot = c; }
            ~CurrentFilter() { slot = nullptr; }
        };

        /** @brief Called periodically while processing so progress can be
         * reported (ParDB cannot service its message loop meanwhile) */
        function<void()> progress_cb_;
        steady_clock::duration progress_interval_ = seconds(1);
        steady_clock::time_point next_progress_;

        /** @brief Call the progress callback if the interval has passed */
        void report_progress_if_due_() {
            if (!progress_cb_) return;
            auto now = steady_clock::now();
            if (now < next_progress_) return;
            next_progress_ = now + progress_interval_;
            progress_cb_();
        }

        /** @brief Run the installed single-entry filters for one processing pass
         *
         * Entries are scanned with a cursor that carries over between passes
         * (see scan_keys_): when a pass stops early (max_filters_per_stage_,
         * the memory policy, or a request from another agent; see
         * check_stop_), the next one continues from where it stopped.
         *
         * @return true if any filter ran. false only once a full lap, over a
         * snapshot of the keys taken during this pass, ran no filter. The DB
         * does not change during a pass (filter output is staged until the
         * stage closes, and nothing is received while filters run), so at
         * that point no entry qualifies for any filter. An entry behind the
         * cursor that becomes runnable at a stage close (from this agent or
         * another) is therefore always picked up on a later lap.
         *
         * Within one pass, an entry is not offered again after any filter has
         * run, since that filter's staged changes are not visible yet.
         */
        bool run_filters_() {
            bool filter_ran = false;

            // Limit how much we run before a stage close to help with memory
            // usage (see max_filters_per_stage_ and mem_policy_). The limits
            // are checked after all filters have been offered an entry, so a
            // pass can go over them by up to one entry's worth of filter runs.
            size_t num_filters_run = 0;
            filters_run_last_pass_ = 0;
            begin_stop_checks_();

            // Look up each SINGLE_ENTRY filter's counters once, not per entry
            vector<pair<Filter*, FilterCounters*>> filters;
            for (auto & [filter_name, filter] : installed_filters_) {
                // Only run on filters that are SINGLE_ENTRY filters
                if (filter->filter_type() != SINGLE_ENTRY) continue;
                filters.push_back({filter.get(), &stats_.filter(filter_name, PASS_SINGLE).pass});
            }

            #ifdef VERBOSE
            size_t i = 0;   // entries this pass
            #endif

            // With continuous processing off, an agent whose sweep is finished
            // does nothing while the rest of the mesh finishes theirs
            if (!continuous_processing_ && sweep_done_) {
                stats_.stopped_early = false;
                return false;
            }

            // Start a new lap on a fresh snapshot if the last one finished
            if (scan_pos_ == 0 || scan_pos_ >= scan_keys_.size()) {
                scan_keys_ = db_.keys();
                scan_pos_ = 0;
            }
            bool lap_from_start = (scan_pos_ == 0);

            stats_.stopped_early = false;

            while (true) {
                // Progress is reported as the cursor's position in the current
                // lap, so a pass that resumes mid-lap starts part-way through
                stats_.keys_total = scan_keys_.size();
                stats_.keys_scanned = scan_pos_;

                while (scan_pos_ < scan_keys_.size()) {
                  // Fetch the keys and tags of the next block of entries with
                  // one request to the map, instead of one request per key.
                  // Values are fetched only for entries a filter will run on
                  // (see ScanLookahead). The block is not kept past this pass:
                  // a stage close can change any entry.
                  size_t block_begin = scan_pos_;
                  size_t block_keys = min(scan_block_keys_, scan_keys_.size() - block_begin);
                  vector<DBEntry<>> block = db_.retrieve_tags_by_keys(scan_keys_, block_begin, block_keys);
                  size_t next_in_block = 0;
                  ScanLookahead la;

                  for (size_t p = block_begin; p < block_begin + block_keys; ++p) {
                    dbkey_t key = scan_keys_[p];
                    scan_pos_ = p + 1;
                    // The snapshot can be older than this pass; skip anything
                    // gone (the block holds only the keys that still exist,
                    // in key order)
                    bool exists = next_in_block < block.size() && block[next_in_block].get_key() == key;
                    ++entries_examined_;
                    if (exists) {
                        size_t b = next_in_block++;
                        #ifdef VERBOSE
                        if (++i % 10000 == 0) {
                            cerr << "I have processed " << i << " entries this pass (cursor at " << scan_pos_ << "/"
                                 << scan_keys_.size() << ") and run " << num_filters_run << " filters" << endl;
                        }
                        #endif
                        // Ask should_run about this entry and the next few
                        // (tags only), unless that was done already
                        if (b >= la.end) scan_decide_window_(block, b, filters, la);
                        const uint8_t* wants = la.wants.data() + (b - la.begin) * filters.size();

                        bool any = false;
                        for (size_t f = 0; f < filters.size(); ++f) {
                            ++filters[f].second->checked;
                            any |= wants[f] != 0;
                        }

                        // Only now is the value needed; nullptr if the entry
                        // is gone since its tags were read
                        DBEntry<>* entry = any ? scan_next_value_(la) : nullptr;
                        for (size_t f = 0; entry != nullptr && f < filters.size(); ++f) {
                            if (!wants[f]) continue;
                            auto & [filter, counters] = filters[f];
                            // A fresh access for each run, with the value
                            DBAccess* access = entry->access();
                            add_db_access(access);

                            filter_ran = true;
                            auto start = steady_clock::now();
                            {
                                CurrentFilter current { cur_filter_, counters };
                                filter->run(access);
                            }
                            counters->run_ns += duration_cast<nanoseconds>(steady_clock::now() - start).count();
                            ++counters->ran;
                            ++num_filters_run;
                            ++filters_run_last_pass_;

                            delete access;
                        }
                    }

                    stats_.keys_scanned = scan_pos_;
                    report_progress_if_due_();

                    uint8_t reason = check_stop_(num_filters_run);
                    // A remote stop can come before any run. If it comes on
                    // the last entry of a lap that began at the first key, the
                    // lap is complete and ran nothing: let the end-of-lap
                    // handling below report that we are done, rather than
                    // stopping and asking for another pass.
                    if (reason != STOP_NONE && num_filters_run == 0 && lap_from_start
                            && scan_pos_ >= scan_keys_.size())
                        reason = STOP_NONE;
                    if (reason != STOP_NONE) {
                        if (!continuous_processing_ && scan_pos_ >= scan_keys_.size()) {
                            // Stopped on the last entry of the snapshot: the
                            // sweep is done, as below. The reason is kept, so
                            // a stop for memory still asks the others to stop.
                            stats_.stop_reason = reason;
                            sweep_done_ = true;
                            reset_scan_();
                            return false;
                        }
                        #ifdef VERBOSE
                        cerr << "breaking early: " << stop_reason_name(reason) << " (cursor at " << scan_pos_ << "/"
                             << scan_keys_.size() << ", " << num_filters_run << " filters run, "
                             << stats_.bytes_emitted << " bytes emitted)" << endl;
                        #endif
                        // Keep the cursor; the next pass continues from here
                        // (fetching the rest of this block again). The lap is
                        // unfinished, so returning true (process again) is
                        // correct. Passes still make progress: the run limit
                        // and memory checks need a run here (the limit is
                        // >= 1), and a remote stop before any run is only
                        // honored if the pass ran a filter elsewhere (see
                        // check_stop_). Either way at least one entry was
                        // scanned, so the cursor has moved.
                        stats_.stopped_early = scan_pos_ < scan_keys_.size();
                        stats_.stop_reason = reason;
                        return true;
                    }
                  }
                }

                // Reached the end of the snapshot
                if (!continuous_processing_) {
                    // Every entry in the snapshot has been offered to the
                    // filters once: the sweep is done, even if filters ran.
                    // Nothing is rechecked, so entries created or made
                    // eligible during the sweep wait for the next request.
                    sweep_done_ = true;
                    reset_scan_();
                    return false;
                }
                if (filter_ran) {
                    // Next pass starts a new lap, which will see this pass's changes
                    reset_scan_();
                    return true;
                }
                if (lap_from_start) {
                    // A full lap of a current snapshot ran nothing: done
                    #ifdef VERBOSE
                    cerr << "done processing" << endl;
                    #endif
                    reset_scan_();
                    return false;
                }
                // Nothing ran, but this pass began mid-snapshot, so entries
                // before the cursor have not been checked in their current
                // state. Nothing is staged yet, so it is safe to start a full
                // lap on a fresh snapshot within this same pass.
                scan_keys_ = db_.keys();
                scan_pos_ = 0;
                lap_from_start = true;
            }
        }


    public:

        /** @brief Create a database with no initial state
         * NOTE: localhost IP addresses used as default values for test cases
         * */
        SeqDB() : db_server_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}),
            db_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}, ZMQAddress {"127.0.0.1", 0}) { ensure_python_initialized(); }

        /** @brief Create a database with an agent id to use as seed */
        SeqDB(ZMQAddress addr) : db_server_(addr+MAP_LOCALNUM_OFFSET), db_(addr+MAP_LOCALNUM_OFFSET, addr) {
            set_agent_id(addr.serialize());
            ensure_python_initialized();
        }

        /** @brief Create a database with an agent id to use as seed and allocator size*/
        SeqDB(ZMQAddress addr, size_t size) : db_server_(addr+MAP_LOCALNUM_OFFSET, size), db_(addr+MAP_LOCALNUM_OFFSET, addr) {
            set_agent_id(addr.serialize());
            ensure_python_initialized();
        }

        /** @brief Build a SeqDB from an input file */
        SeqDB(string fn) : db_server_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}),
                db_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}, ZMQAddress {"127.0.0.1", 0}) {
            add_db_file(fn);

            ensure_python_initialized();
        }

        // Python is intentionally not finalized here; see ensure_python_initialized().
        ~SeqDB() = default;

        /** @brief Build a SeqDB from an input file with a DB size */
        SeqDB(string fn, size_t size) : db_server_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}, size),
                                        db_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}, ZMQAddress {"127.0.0.1", 0}) {
            add_db_file(fn);
            ensure_python_initialized();
        }

        /** @brief Add an input file
         *
         * The file should be an ASCII file.  If a line consists of TAGS, then
         * the following lines will be newline-separate tag entries.  If a line
         * consists of VALUE, then the following lines will be parsed as the
         * raw value to include.  If a line consists of END, the entry will be
         * closed and inserted into the database.
         */
        void add_db_file(string fn) {
            // Keep track of the DB file parsing state
            parse_state_ state = NOENTRY;

            // Open the file
            if (ifstream in_file {fn}) {
                // Read the file line-by-line
                string line;

                // Current entry to insert
                DBEntry<> entry;

                while (getline(in_file, line)) {
                    // Check for different database action
                    if (state == NOENTRY) {
                        if (line == "TAGS") {
                            state = TAGS;
                            continue;
                        }
                    }
                    if (state == TAGS) {
                        if (line == "VALUE") {
                            state = VALUE;
                            continue;
                        }

                        // Add the tags 
                        entry.add_tag(line);
                    }

                    if (state == VALUE) {
                        if (line == "END") {
                            // Insert the parsed entry into the database
                            add_entry(move(entry));

                            // Reset the state
                            state = NOENTRY;

                            // Reset the current entries
                            DBEntry<> new_entry;
                            entry = move(new_entry);

                            continue;
                        }

                        // Add to the value
                        entry.add_to_value(line);
                    }
                }
            } else
                cerr << "Unable to open test file, skipping" << endl;
        }

        virtual void subscribe_to_entry_wrapper(dbkey_t my_key, dbkey_t wait_key, string inactive_tag) {
            subscribe_to_entry(my_key, wait_key, inactive_tag);
        }

        /** @brief have one entry subscribe (wait) for another to be created */
        void subscribe_to_entry(dbkey_t my_key, dbkey_t wait_key, string inactive_tag) {
            entry_subscriptions_[wait_key].push_back({my_key, inactive_tag});
        }

        void handle_subscriptions(dbkey_t wait_key) {
            for (auto& [key, tag] : entry_subscriptions_[wait_key]) {
                stage_remove_tag_worker(key, tag);
            }
            entry_subscriptions_.erase(wait_key);
        }

        virtual void add_entry(DBEntry<> entry) {
            add_entry_worker(move(entry));
        }

        void prepare_to_add_entry(DBEntry<>* entry) {
            if (entry->get_key() == INITIAL_KEY) {
                entry->set_key(generate_random_key());
            }

            dbkey_t key = entry->get_key();
            handle_subscriptions(key);

            //If the key already exists, we concatenate the entries' values and tags
            auto [e, exists] = db_.retrieve_if_exists(entry->get_key());
            if (exists) {

                // if the entries are different, or if either entry has the
                // "MERGE_STRATEGY=FORCE_MERGE" tag, then concatenate them
                bool should_concat = e != *entry;
                bool force_merge = e.has_tag(MERGE_STRATEGY_FORCE);
                force_merge |= entry->has_tag(MERGE_STRATEGY_FORCE);
                should_concat |= force_merge;

                if (e.has_tag(MERGE_STRATEGY_SUM) && entry->has_tag(MERGE_STRATEGY_SUM)) {
                    should_concat = false;
                    *entry = sum_entries(&e, entry);
                }

                if (should_concat)
                    *entry = concat_entries(&e, entry, force_merge);
            }

            // Index the tags after merging, so tags the merge added (MERGED,
            // MERGED_SUM, MERGED_SUM_FAILED) are indexed too
            entry->tag_store().for_each([&](const string& tag) {
                add_to_tag_index_(key, tag);
            });
        }

        /** @brief Add a specific DB entry */
        void add_entry_worker(DBEntry<> entry) {
            prepare_to_add_entry(&entry);
            db_.insert(move(entry));
        }

        void add_entries(absl::flat_hash_map<dbkey_t, DBEntry<>>* entries) {
            for (auto & [key, entry] : *entries) {
                prepare_to_add_entry(&entry);
            }
            db_.insert_multiple(entries);
        }

        /** @brief A parsed MERGE_STRATEGY=SUM value */
        struct sum_value_t {
            bool is_int;
            __int128 i;     // valid if is_int
            double d;       // always valid
        };

        /** @brief Parse an optionally negative base-10 integer into an __int128
         * @return false if s is not an integer or does not fit
         */
        static bool parse_sum_int_(string_view s, __int128& out) {
            bool neg = false;
            if (!s.empty() && s[0] == '-') { neg = true; s.remove_prefix(1); }
            if (s.empty()) return false;
            // Accumulate the magnitude; the negative range is one larger
            const unsigned __int128 limit = neg
                ? (unsigned __int128)1 << 127
                : ((unsigned __int128)1 << 127) - 1;
            unsigned __int128 mag = 0;
            for (char c : s) {
                if (c < '0' || c > '9') return false;
                unsigned d = c - '0';
                if (mag > (limit - d) / 10) return false;
                mag = mag * 10 + d;
            }
            if (mag == 0) out = 0;
            else out = neg ? -(__int128)(mag - 1) - 1 : (__int128)mag;
            return true;
        }

        /** @brief Parse a value to sum. Leading and trailing whitespace is
         * ignored, so values built with DBEntry::add_to_value() (which ends
         * them with a newline) can be summed.
         * @return false if the value is not a finite number
         */
        static bool parse_sum_value_(const string& value, sum_value_t& out) {
            static const char* ws = " \t\n\r\f\v";
            size_t b = value.find_first_not_of(ws);
            if (b == string::npos) return false;
            size_t e = value.find_last_not_of(ws);
            string_view s {value.data() + b, e - b + 1};

            if (parse_sum_int_(s, out.i)) {
                out.is_int = true;
                out.d = (double)out.i;
                return true;
            }

            double d = 0;
            auto [ptr, ec] = from_chars(s.data(), s.data() + s.size(), d);
            if (ec != errc() || ptr != s.data() + s.size() || !isfinite(d)) return false;
            out.is_int = false;
            out.d = d;
            return true;
        }

        static string format_sum_int_(__int128 v) {
            bool neg = v < 0;
            unsigned __int128 mag = neg ? (unsigned __int128)(-(v + 1)) + 1 : (unsigned __int128)v;
            string digits;
            do { digits += (char)('0' + (int)(mag % 10)); mag /= 10; } while (mag != 0);
            if (neg) digits += '-';
            return string(digits.rbegin(), digits.rend());
        }

        /** @brief Shortest decimal form that reads back as the same double */
        static string format_sum_double_(double v) {
            char buf[64];
            auto [ptr, ec] = to_chars(buf, buf + sizeof(buf), v);
            if (ec != errc()) throw runtime_error("Unable to format summed value");
            return string(buf, ptr);
        }

        /** @brief Merge two MERGE_STRATEGY=SUM entries by adding their values
         *
         * If both values are integers, they are added exactly (128-bit) and
         * stored as an integer, e.g. "1" + "2" = "3". Otherwise they are
         * added as doubles and stored in the shortest form that reads back
         * exactly, e.g. "1.2" + "2.3" = "3.5". The result is tagged
         * MERGED_SUM.
         *
         * If either value is not a number, the entries are concatenated
         * instead, as concat_entries does, and tagged MERGED_SUM_FAILED, so
         * no data is lost. Later merges on that key will also fail; a warning
         * is printed the first time a key fails.
         */
        DBEntry<> sum_entries(DBEntry<>* e1, DBEntry<>* e2) {
            sum_value_t v1 {}, v2 {};
            if (!parse_sum_value_(e1->value(), v1) || !parse_sum_value_(e2->value(), v2)) {
                if (!e1->has_tag(MERGED_SUM_FAILED) && !e2->has_tag(MERGED_SUM_FAILED)) {
                    cerr << "[Pando] [WARNING] " << MERGE_STRATEGY_SUM << ": value for key " << e1->get_key()
                         << " is not a number; concatenating instead (tagged " << MERGED_SUM_FAILED << ")" << endl;
                }
                DBEntry<> failed = concat_entries(e1, e2);
                failed.add_tag(MERGED_SUM_FAILED);
                return failed;
            }

            DBEntry<> new_entry;
            new_entry.set_key(e1->get_key());
            new_entry.copy_tags_from(*e1);
            e2->tag_store().for_each([&](const string& tag) { new_entry.add_tag(tag); });
            new_entry.add_tag(MERGED_SUM);

            __int128 isum = 0;
            if (v1.is_int && v2.is_int && !__builtin_add_overflow(v1.i, v2.i, &isum))
                new_entry.value() = format_sum_int_(isum);
            else
                new_entry.value() = format_sum_double_(v1.d + v2.d);

            return new_entry;
        }

        DBEntry<> concat_entries(DBEntry<>* e1, DBEntry<>* e2, bool force_merge=false) {
            DBEntry<> new_entry;
            new_entry.value() = e1->value() + '\n' + e2->value();

            #ifdef VERBOSE
            cerr << "MERGING entries, new size = " << new_entry.value().size() << endl;
            #endif
            //Assume e1 and e2 have the same key
            dbkey_t new_key = e1->get_key();
            new_entry.set_key(new_key);

            // The union of both entries' tags (with force_merge, assume they
            // have the same tags and take e1's), plus MERGED
            new_entry.copy_tags_from(*e1);
            if (!force_merge)
                e2->tag_store().for_each([&](const string& tag) { new_entry.add_tag(tag); });
            new_entry.add_tag("MERGED");

            return new_entry;
        }

        virtual void stage_add_entry(DBEntry<> entry) {
            stage_add_entry_worker(move(entry));
        }

        /** @brief Set an entry to be added later in bulk */
        void stage_add_entry_worker(DBEntry<> entry) {
            if (entry.get_key() == INITIAL_KEY) {
                entry.set_key(generate_random_key());
            }

            //If the key already exists, we concatenate the entries' values and tags
            auto new_entries_it = new_entries_.find(entry.get_key());
            if (new_entries_it != new_entries_.end()) {

                // if the entries are different, or if either entry has the
                // "MERGE_STRATEGY=FORCE_MERGE" tag, then concatenate them
                bool should_concat = new_entries_it->second != entry;
                bool force_merge = entry.has_tag(MERGE_STRATEGY_FORCE);
                force_merge |= new_entries_it->second.has_tag(MERGE_STRATEGY_FORCE);
                should_concat |= force_merge;

                // Sum numeric values when both entries ask for it, as
                // add_entry does. Without this, two identical staged entries
                // (e.g. two values of "1") compare equal and the second is
                // dropped, and differing ones are concatenated instead of added.
                if (new_entries_it->second.has_tag(MERGE_STRATEGY_SUM) && entry.has_tag(MERGE_STRATEGY_SUM)) {
                    new_entries_it->second = sum_entries(&(new_entries_it->second), &entry);
                } else if (should_concat) {
                    entry = concat_entries(&(new_entries_it->second), &entry, force_merge);
                    new_entries_it->second = move(entry);
                }
            } else
                new_entries_.insert({entry.get_key(), move(entry)});
        }

        virtual void stage_add_tag(dbkey_t key, string tag) {
            stage_add_tag_worker(key, tag);
        }

        /** @brief Set a tag to be added to an entry later */
        void stage_add_tag_worker(dbkey_t key, string tag) {
            tags_to_add_.push_back({key, tag});
        }

        /** @brief Set a tag to be removed from an entry later */
        void stage_remove_tag_worker(dbkey_t key, string tag) {
            tags_to_remove_.push_back({key, tag});
        }

        virtual void stage_remove_tag(dbkey_t key, string tag) {
            stage_remove_tag_worker(key, tag);
        }

        /** @brief Close the stage of adding entries, modifying the db */
        virtual void stage_close() {
            add_entries(&new_entries_);
            new_entries_.clear();

            //TODO test case, what if key doesn't exist?
            if (val_updates_.size() != 0) {
                for (auto & [key, new_val] : val_updates_) {
                    update_entry_val(key, new_val);
                }
            }
            val_updates_.clear();

            add_tags_to_entries(&tags_to_add_);
            tags_to_add_.clear();

            for (auto & [key, tag] : tags_to_remove_)
                remove_tag_from_entry(key, tag);
            tags_to_remove_.clear();
        }

        virtual void stage_update_entry_val(dbkey_t key, string new_val) {
            stage_update_entry_val_worker(key, new_val);
        }

        void stage_update_entry_val_worker(dbkey_t key, string new_val) {
            val_updates_[key] = new_val;
        }

        /** @brief Add a filter directory
         *
         * This directory will be searched, along with other added directories,
         * when looking for filters.
         */
        void add_filter_dir(string dir) {
            if (!filesystem::exists(dir)) {
                cerr << "[ERROR] directory " << dir << " did not exist" << endl;
                return;
            }
            for (auto & file : filesystem::recursive_directory_iterator(dir)) {
                if (file.path().extension() != ".so") continue;
                
                filter_p filter;

                // Load the filter in the SO
                try {
                    filter = make_shared<Filter>(file.path());
                } catch(const std::exception &exc) {
                    cerr << "[WARNING] non-filter .so file found: " << file.path() << endl;
                    continue;
                }

                // Grab the filter name
                string filter_name { filter->name() };
                // Ensure that the filter is unique
                if (filters_.count(filter_name) > 0) {
                    cerr << "WARNING: Filter \"" << filter_name << "\" already registered, updating it" << endl;
                }

                // Register it by name
                filters_[filter_name] = filter;
            }
        }

        /** @brief Install a filter to run when processing */
        void install_filter(string filter_name) {
            // Get the filter
            if (filters_.count(filter_name) == 0) {
                cerr << "WARNING: Unable to find filter: " << filter_name << ", skipping" << endl;
                return;
            }

            auto & filter = filters_[filter_name];

            // Install it as any other filter
            install_filter(filter);
        }

        /** @brief Install a filter from memory */
        void install_filter(filter_p filter) {
            string filter_name { filter->name() };
            // Add it to the installed list
            installed_filters_[filter_name] = filter;
            // Register it with the stats now so that it is reported (e.g.,
            // shown by pando_top) before any processing has happened
            stats_.filter(filter_name, filter->filter_type() == GROUP_ENTRIES ? PASS_GROUP : PASS_SINGLE);
            // New filters may apply to entries behind the cursor
            reset_scan_();
        }

        /** @brief Value of max_filters_per_stage() meaning no limit (the default) */
        static constexpr size_t UNLIMITED_FILTERS_PER_STAGE = numeric_limits<size_t>::max();

        /** @brief Set how many filter runs one processing pass may do
         * before stopping so the stage can close (default
         * UNLIMITED_FILTERS_PER_STAGE, i.e. no limit)
         */
        void set_max_filters_per_stage(size_t n) {
            if (n == 0) throw invalid_argument("max_filters_per_stage must be at least 1");
            max_filters_per_stage_ = n;
        }

        /** @brief Return how many filter runs one processing pass may do */
        size_t max_filters_per_stage() const { return max_filters_per_stage_; }

        /** @brief Set when a processing pass ends early to bound memory (see
         * memory_policy.hpp). Applies from the next pass.
         * @throws invalid_argument if a setting is out of range
         */
        void set_memory_policy(const MemoryPolicy& policy) {
            policy.validate();
            mem_policy_ = policy;
        }

        /** @brief Return the memory policy */
        const MemoryPolicy& memory_policy() const { return mem_policy_; }

        /** @brief Replace where system memory is read from (for tests) */
        void set_meminfo_source(function<MemInfo()> source) {
            meminfo_source_ = move(source);
        }

        /** @brief Set how to work out the agents sharing this node's memory
         * when the policy's agents_per_node is 0 (auto). Asked when each pass
         * starts, so it can follow a changing mesh; a result of 0 means 1. */
        void set_agents_per_node_source(function<uint64_t()> source) {
            agents_per_node_source_ = move(source);
        }

        /** @brief The agents the headroom is split between: the policy's
         * agents_per_node if set, else the source's answer, else 1 */
        uint64_t agents_per_node() {
            uint64_t n = mem_policy_.agents_per_node;
            if (n == 0 && agents_per_node_source_) n = agents_per_node_source_();
            return n == 0 ? 1 : n;
        }

        /** @brief Set a check for requests (e.g. from other agents) to end the
         * current pass early. It is called while filters run, after an entry,
         * at most every check_interval (from the memory policy, which need not
         * be enabled for this). Its argument says whether a filter has run in
         * this pass yet.
         *
         * Returning true before any run here ends a pass that did nothing on
         * this DB (it keeps its cursor and reports "process again"). Do that
         * only if the pass made progress elsewhere, e.g. the asking agent ran
         * a filter in it; otherwise passes could repeat forever without any
         * work. ParDB's callback follows that rule (see CloseEarlyState). */
        void set_stop_requested_callback(function<bool(bool have_run)> cb) {
            stop_requested_cb_ = move(cb);
        }

        /** @brief Filter runs in the last run_filters_() (the current one,
         * while it runs). ParDB reads it after its pass's filters, to say in a
         * CLOSE_EARLY whether it ran any. */
        size_t filters_run_last_pass() const { return filters_run_last_pass_; }

        /** @brief Whether system memory is above the limit right now. Uses
         * the reserve computed when the last pass started; false if the
         * policy has no limit_pct or memory could not be read then. */
        bool under_memory_pressure() {
            if (!mem_pressure_check_) return false;
            MemInfo mem = meminfo_source_();
            return mem.ok && mem.available_bytes < mem_reserve_bytes_;
        }

        /** @brief Return memory freed by the stage close to the system, so
         * that the next pass's headroom reflects it. glibc keeps freed memory
         * in its heap otherwise. Only done when the memory policy is enabled. */
        void release_free_memory() {
            #ifdef __GLIBC__
            if (mem_policy_.enabled()) malloc_trim(0);
            #endif
        }

        /** @brief Return how many entries have been offered to the filters in
         * total (each entry fetched by a processing pass counts once)
         */
        size_t entries_examined() const { return entries_examined_; }

        /** @brief Return how many entries' values the filter scan has fetched
         * in total (only entries some filter ran on, or was about to run on
         * when the pass stopped, have their values fetched) */
        size_t scan_values_fetched() const { return scan_values_fetched_; }

        /** @brief Return how many requests the filter scan has made for values */
        size_t scan_value_requests() const { return scan_value_requests_; }

        /** @brief Default number of keys the scan fetches per request */
        static constexpr size_t DEFAULT_SCAN_BLOCK_KEYS = 4096;

        /** @brief Values are fetched only for entries a filter will run on
         * (see ScanLookahead), and each such reply stops growing past about
         * this many bytes, so databases with large values (e.g. whole blocks
         * of a blockchain) do not get huge replies; the rest is fetched with
         * the next request */
        static constexpr size_t SCAN_BLOCK_BYTES = 16u << 20;

        /** @brief should_run is asked about this many entries of a block at a
         * time, before any of them run, so the values those entries need can
         * be fetched with one request. Asking ahead does work for entries a
         * pass that stops early will not reach (they are asked again next
         * pass), so this bounds that waste. */
        static constexpr size_t SCAN_LOOKAHEAD_ENTRIES = 256;

        /** @brief Set how many keys the filter scan fetches from the map per
         * request. Larger blocks mean fewer requests; each block's keys and
         * tags are held in memory while it is scanned. Values are not part of
         * the block: they are fetched only for the entries a filter runs on,
         * in replies capped at about SCAN_BLOCK_BYTES. */
        void set_scan_block_keys(size_t n) {
            if (n == 0) throw invalid_argument("scan_block_keys must be at least 1");
            scan_block_keys_ = n;
        }

        /** @brief Return how many keys the filter scan fetches per request */
        size_t scan_block_keys() const { return scan_block_keys_; }

        /** @brief Whether changes are staged, waiting for a stage close */
        bool has_staged_changes() const {
            return !new_entries_.empty() || !val_updates_.empty() ||
                   !tags_to_add_.empty() || !tags_to_remove_.empty();
        }

        /** @brief Return the number of installed filters*/
        size_t num_filters() {return installed_filters_.size();}

        /** @brief return a list of the installed filter names */
        vector<string> installed_filters() {
            vector<string> ret;
            for (auto &[filter_name, filter] : installed_filters_) {
                ret.push_back(filter_name);
            }
            return ret;
        }

        /** @brief Clear the installed filters */
        void clear_filters() {
            installed_filters_.clear();
            reset_scan_();
        }

        /** @brief Process one iteration, returning whether any filters were ran */
        bool process_once() {
            stats_.begin_pass(PASS_SINGLE);
            // Close any pending changes
            bool filter_ran = run_filters_();
            stage_close();
            release_free_memory();
            stats_.end_pass();
            return filter_ran;
        }

        /** @brief Processing statistics (per filter, per pass) */
        ProcessStats& stats() { return stats_; }
        const ProcessStats& stats() const { return stats_; }

        /** @brief Call cb at most once per interval while processing */
        void set_progress_callback(function<void()> cb, steady_clock::duration interval = seconds(1)) {
            progress_cb_ = move(cb);
            progress_interval_ = interval;
            next_progress_ = steady_clock::now() + interval;
        }

        /** @brief Called by the filter API to attribute outputs to the
         * running filter; no-ops outside of a filter run */
        void note_entry_out() { if (cur_filter_ != nullptr) ++cur_filter_->entries_out; }
        void note_tag_added() { if (cur_filter_ != nullptr) ++cur_filter_->tags_added; }
        void note_tag_removed() { if (cur_filter_ != nullptr) ++cur_filter_->tags_removed; }
        void note_val_update() { if (cur_filter_ != nullptr) ++cur_filter_->val_updates; }
        /** @brief Count output a filter created against this pass's byte
         * budget (see ProcessStats::bytes_emitted); no-op outside a filter run */
        void note_bytes_out(size_t n) { if (cur_filter_ != nullptr) stats_.bytes_emitted += n; }

        /** @brief Process the database, applying filters as appropriate */
        virtual bool process() {
            start_new_sweep();
            while (process_once()) {}
            return false;
        }

        /** @brief Choose how a process request ends
         *
         * On (the default), "continuous processing": sweep over the entries,
         * and keep sweeping until a whole sweep runs no filter. A filter that
         * makes another entry eligible (by creating or tagging it) is
         * therefore always followed up, at the cost of one more sweep after
         * the last one that ran anything.
         *
         * Off: process each entry once per request. One sweep is made: every
         * entry that existed when it started is offered to the filters once
         * (over as many passes as max_filters_per_stage or the memory policy
         * need), and nothing is rechecked. Use this only when no filter's
         * writes can make an entry eligible for a filter in the same request:
         * entries created or made eligible during the sweep are left for the
         * next request.
         */
        void set_continuous_processing(bool on) { continuous_processing_ = on; }

        /** @brief Whether a process request keeps sweeping until nothing runs */
        bool continuous_processing() const { return continuous_processing_; }

        /** @brief Begin a new process request: its sweep starts from the first
         * entry of a fresh snapshot */
        void start_new_sweep() {
            sweep_done_ = false;
            reset_scan_();
        }

        /** @brief Return the current size of the database */
        size_t size() { return db_.size(); }

        vector<dbkey_t> keys() {
            return db_.keys();
        }

        /** @brief Return the database entries */
        absl::flat_hash_map<dbkey_t, DBEntry<>> entries() {
            absl::flat_hash_map<dbkey_t, DBEntry<>> entries;
            for (auto& key : db_.keys()) {
                entries[key] = db_.retrieve(key);
            }
            return entries;
        }

        /** @brief Add DB access fields that modify globally */
        virtual void add_db_access_mod(DBAccess* access) {
            access->make_new_entry.state = this;
            access->make_new_entry.run = &s_SeqDB_new_entry;

            access->add_tag.state = access;
            access->add_tag.run = &s_SeqDB_add_tag;

            access->remove_tag.state = access;
            access->remove_tag.run = &s_SeqDB_remove_tag;

            access->subscribe_to_entry.state = access;
            access->subscribe_to_entry.run = &s_SeqDB_subscribe_to_entry;

            access->update_entry_val.state = access;
            access->update_entry_val.run = &s_SeqDB_update_entry_val;
        }

        /** @brief Add DB access fields that retrieve */
        virtual void add_db_access_ret(DBAccess* access) {
            access->get_entry_by_tags.state = this;
            access->get_entry_by_tags.run = &s_SeqDB_get_entry_by_tags;

            access->get_entry_by_key.state = this;
            access->get_entry_by_key.run = &s_SeqDB_get_entry_by_key;

            access->get_entries_by_keys.state = this;
            access->get_entries_by_keys.run = &s_SeqDB_get_entries_by_keys;

            access->get_entries_by_tags.state = this;
            access->get_entries_by_tags.run = &s_SeqDB_get_entries_by_tags;
        }

        /** @brief Add DB access fields to an entry access */
        virtual void add_db_access(DBAccess* access) {
            add_db_access_mod(access);
            add_db_access_ret(access);
        }

        void add_tags_to_entries(vector<pair<dbkey_t, string>>* to_add) {
            absl::flat_hash_map<dbkey_t, DBEntry<>> to_insert;
            for (auto & [key, tag] : *to_add) {
                auto idx = to_insert.find(key);
                if (idx == to_insert.end()) {
                    //haven't retrieved this entry yet, retrieve it
                    auto entry = db_.retrieve(key); //TODO also batch retrievals
                    entry.add_tag(tag);
                    to_insert[key] = move(entry);
                    add_to_tag_index_(key, tag);
                } else {
                    idx->second.add_tag(tag);
                    add_to_tag_index_(key, tag);
                }
            }

            db_.insert_multiple(&to_insert);
        }

        /** @brief Add tags to an entry in the database at the given key */
        template<typename ...Args>
        void add_tag_to_entry(dbkey_t k, Args && ...args) {
            // Add the tags appropriately
            DBEntry<> e = db_.retrieve(k);
            e.add_tag(args...);
            db_.insert(move(e));
            // Update the tag index
            add_to_tag_index_(k, args...);
        }

        /** @brief Remove tags from an entry in the database at the given key */
        template<typename ...Args>
        void remove_tag_from_entry(dbkey_t k, Args && ...args) {
            // Remove the tags appropriately
            DBEntry<> e = db_.retrieve(k);
            e.remove_tag(args...);
            db_.insert(move(e));
            // Update the tag index
            remove_from_tag_index_(k, args...);
        }

        /** @brief Update the value of an entry at the given key */
        void update_entry_val(dbkey_t k, string val) {
            DBEntry<> e = db_.retrieve(k);
            e.value() = val;
            db_.insert(move(e));
        }

        /** @brief Return an entry based on given C tags
         *
         * @param c_tags a C-tags style list of tags
         * @param res this will be a pointer to a character pointer (C string).
         *        It will be allocated with the value of the first resulting
         *        entry that matches the given tags.  The caller is responsible
         *        for freeing the memory.
         *        If res is NULL, nothing matched.
         */
        void get_entry_value_by_tags(const char* const* c_tags, char **res) {
            set<dbkey_t> matching_entries = get_entry_by_tags_(c_tags);

            *res = nullptr;

            if (matching_entries.size() == 0) return;

            DBEntry<> entry = db_.retrieve(*matching_entries.begin());

            // This is the first entry to match
            // Return its value
            auto & value = entry.value();
            *res = (char*)malloc(sizeof(char)*value.size()+1);
            if (*res == nullptr) throw runtime_error("Unable to allocate memory");
            memcpy(*res, value.c_str(), value.size()+1);
        }

        /** @brief Get the value of the entry that has key search_key */
        void get_entry_value_by_key_worker(dbkey_t search_key, char **res) {
            *res = nullptr;

            auto [entry, exists] = db_.retrieve_if_exists(search_key);
            if (!exists) return;

            auto & value = entry.value();
            *res = (char*)malloc(sizeof(char)*value.size()+1);
            if (*res == nullptr) throw runtime_error("Unable to allocate memory");
            strcpy(*res, value.c_str());
        }

        /** @brief Get the value of the entry that has key search_key */
        virtual void get_entry_value_by_key(dbkey_t search_key, char **res) {
            get_entry_value_by_key_worker(search_key, res);
        }

        /** @brief Read one value from this DB's own map, with its exact
         * length (values may contain NUL bytes)
         * @return false if the key does not exist
         */
        bool get_value_if_exists(dbkey_t key, string& value) {
            ZMQMessage resp = db_.retrieve_values_raw(1, &key);
            const char* data = resp.data();
            const char* end = data + resp.size();
            if ((size_t)(end - data) < 2*sizeof(size_t) + sizeof(bool)) throw runtime_error("values reply: too short");
            size_t count;
            bool exists;
            size_t len;
            unpack_single(data, count);
            unpack_single(data, exists);
            unpack_single(data, len);
            if (count != 1 || (size_t)(end - data) < len) throw runtime_error("values reply: malformed");
            value.assign(data, len);
            return exists;
        }

        /** @brief Get the values for n keys with one request to the map
         *
         * values[i] receives a malloc'd, NUL-terminated copy of the value for
         * keys[i] (the caller frees it), or nullptr if the key does not exist.
         * An empty value gives an empty string, not nullptr. Keys may repeat.
         */
        void get_entry_values_by_keys_worker(size_t n, const dbkey_t* keys, char** values) {
            for (size_t i = 0; i < n; ++i) values[i] = nullptr;
            if (n == 0) return;

            ZMQMessage resp = db_.retrieve_values_raw(n, keys);
            const char* data = resp.data();
            try {
                unpack_values_reply(data, data + resp.size(), n, nullptr, values);
            } catch (...) {
                for (size_t i = 0; i < n; ++i) { free(values[i]); values[i] = nullptr; }
                throw;
            }
        }

        /** @brief Get the values for n keys (see get_entry_values_by_keys_worker);
         * ParDB's version asks every owning agent at once */
        virtual void get_entry_values_by_keys(size_t n, const dbkey_t* keys, char** values) {
            get_entry_values_by_keys_worker(n, keys, values);
        }


        /** @brief Remove a tag from an entry found by tags */
        void remove_tag_from_entry(const char* const* search_tags, string tag) {
            auto matching_entries = get_entry_by_tags_(search_tags);
            auto key = *matching_entries.begin();
            DBEntry<> e = db_.retrieve(key);
            e.remove_tag(tag);
            db_.insert(move(e));
        }

        /** @brief Get all entries with the given tags and that return success given a query function */
        vector<string> get_entries_by_tags(const char* const* c_tags, const DBAccess* access, db_query_t query_func) {
            vector<string> ret;
            auto keys = get_entry_by_tags_(c_tags);
            for (auto& key : keys) {
                DBEntry<> e = db_.retrieve(key);
                if (query_func((dbkey_t)key, e.value().c_str(), e.c_tags(), access)) {
                    ret.push_back(string{e.value()});
                }
            }
            return ret;
        }

        /** @brief Get the DB keys for all entries that match the given tags*/
        set<dbkey_t> get_entry_by_tags(const char* const* c_tags) {
            return get_entry_by_tags_(c_tags);
        }
        set<dbkey_t> get_entry_by_tags(set<string> tags) {
            std::allocator<char> alloc;
            DBEntry<> e;
            for (auto & tag : tags) e.add_tag(tag);
            return get_entry_by_tags_(e.c_tags());
        }

        vector<dbkey_t> query(vector<string> tags) {
            set<string> tags_set (tags.begin(), tags.end());
            set<dbkey_t> keys = get_entry_by_tags(tags_set);
            vector<dbkey_t> ret(keys.begin(), keys.end());
            return ret;
        }

        dbkey_t generate_random_key() {
            return random_key_gen_.get();
        }

        void set_agent_id(uint64_t id) {
            agent_id_ = id;
            initialize_random_key_gen_();
        }

        void verify_entry_key(DBEntry<>* e) {
            random_key_gen_.verify_entry_key(e);
        }

        vector<dbkey_t> get_keys() {
            return db_.keys();
        }

        size_t serialize_size(vector<dbkey_t> key_list) {
            size_t ser_size = 0;
            ser_size += sizeof(size_t);
            for (auto & key : key_list) {
                DBEntry<> entry = db_.retrieve(key);
                size_t entry_size = entry.serialize_size();
                ser_size += sizeof(size_t);
                ser_size += entry_size;
            }

            return ser_size;
        }

		size_t serialize_size() {
            return serialize_size(db_.keys());
        }

        //serialize format:
        //<num_entries (size_t)>
        //  <size of entry (size_t)>
        //  <entry data (variable length)>
        //  <size of entry (size_t)>
        //  <entry data (variable length)>
        void serialize_entries(char*& ser_ptr) {
            serialize_entries(ser_ptr, db_.keys());
            return;
        }

        void serialize_entries(char*& ser_ptr, vector<dbkey_t> key_list) {
            //copy in the data

            //first, copy in the num entries in the db
            size_t num_entries = key_list.size();
            *(size_t*)ser_ptr = num_entries; ser_ptr += sizeof(size_t);

            for (auto & key : key_list) {
                DBEntry<> entry = db_.retrieve(key);
                //before each entry, write a size_t of how big the entry is
                size_t ser_size = entry.serialize_size();
                *(size_t*)ser_ptr = ser_size; ser_ptr += sizeof(size_t);
                //Write the actual entry data
                entry.serialize(ser_ptr);
            }

        }

        void export_db(string export_fn) {
            size_t ssize = serialize_size();

            pigo::WFile out_f {export_fn, ssize};

            char* data_ptr = (char*)out_f.fp();

            serialize_entries(data_ptr);
        }

        void export_db_with_tag(string export_fn, string tag) {
            set<string> tags {tag};
            set<dbkey_t> keys_set = get_entry_by_tags(tags);
            vector<dbkey_t> keys(keys_set.begin(), keys_set.end());

            size_t ssize = serialize_size(keys);

            pigo::WFile out_f {export_fn, ssize};

            char* data_ptr = (char*)out_f.fp();

            serialize_entries(data_ptr, keys);
        }

        /** @brief Walk one serialized DBEntry occupying exactly [p, p + size)
         *
         * Calls f(string_view) for each tag. Checks every length against the
         * bounds, so it is safe on untrusted data.
         *
         * @return false if the bytes are not exactly one well-formed entry
         */
        template<typename F>
        static bool walk_serialized_entry_(const char* p, size_t size, F&& f) {
            const char* end = p + size;
            auto read_len = [&](size_t& len) {
                if ((size_t)(end - p) < sizeof(size_t)) return false;
                len = *(const size_t*)p; p += sizeof(size_t);
                return true;
            };

            if (size < sizeof(dbkey_t)) return false;
            p += sizeof(dbkey_t);

            size_t val_size;
            if (!read_len(val_size) || (size_t)(end - p) < val_size) return false;
            p += val_size;

            size_t ntags;
            if (!read_len(ntags)) return false;
            for (; ntags > 0; --ntags) {
                size_t tag_size;
                if (!read_len(tag_size) || (size_t)(end - p) < tag_size) return false;
                f(string_view {p, tag_size});
                p += tag_size;
            }
            return p == end;
        }

        /** @brief Import entries from a file written by export_db
         *
         * Entries are inserted by the PandoMap straight from the mapped file
         * (see PandoMapClient::insert_from_buffer), so each value is copied
         * only once. Entries whose key already exists (or repeats within the
         * file), or that have no key, go through add_entry as before, so they
         * are merged the same way.
         *
         * The new entries skip the virtual add_entry: an override of it is
         * not called for them.
         */
        void import_db(string import_fn) {
            pigo::ROFile in_f {import_fn};
            const char* ser = in_f.fp();
            const char* end = ser + in_f.size();

            auto bad_file = [&](const string& why) {
                return runtime_error("import_db: " + import_fn + " is malformed (" + why + ")");
            };

            if ((size_t)(end - ser) < sizeof(size_t)) throw bad_file("no entry count");
            size_t num_entries = *(const size_t*)ser; ser += sizeof(size_t);
            // Each entry takes at least its size prefix, so this bounds the
            // reserve below against a garbage count
            if (num_entries > (size_t)(end - ser) / sizeof(size_t)) throw bad_file("entry count too large");

            // Check every entry before the map reads any of them
            vector<const char*> entries;
            entries.reserve(num_entries);
            for (size_t i = 0; i < num_entries; ++i) {
                if ((size_t)(end - ser) < sizeof(size_t)) throw bad_file("truncated entry size");
                size_t entry_size = *(const size_t*)ser; ser += sizeof(size_t);
                if ((size_t)(end - ser) < entry_size ||
                        !walk_serialized_entry_(ser, entry_size, [](string_view) {}))
                    throw bad_file("bad entry " + to_string(i));
                entries.push_back(ser);
                ser += entry_size;
            }

            if (entries.empty()) return;

            vector<size_t> skipped = db_.insert_from_buffer(entries.data(), entries.size());

            // In file order, so repeated keys merge in the same order as
            // adding the entries one at a time would
            size_t next_skipped = 0;
            for (size_t i = 0; i < entries.size(); ++i) {
                const char* p = entries[i];
                if (next_skipped < skipped.size() && skipped[next_skipped] == i) {
                    ++next_skipped;
                    DBEntry<> e(p);
                    add_entry(move(e));
                    continue;
                }

                // Inserted by the map: do the rest of what prepare_to_add_entry
                // does for a new key
                dbkey_t key = *(const dbkey_t*)p;
                handle_subscriptions(key);
                size_t entry_size = *(const size_t*)(p - sizeof(size_t));
                walk_serialized_entry_(p, entry_size, [&](string_view tag) {
                    auto it = entries_by_tag_.find(tag);
                    if (it == entries_by_tag_.end())
                        it = entries_by_tag_.emplace(string{tag}, set<dbkey_t>{}).first;
                    it->second.insert(key);
                });
            }
        }

        void deserialize_and_add_entries(const char* ser) {
            size_t num_entries = *(const size_t*)ser; ser += sizeof(size_t);

            for (size_t i = 0; i < num_entries; i++) {
                size_t entry_size = *(const size_t*)ser; ser += sizeof(size_t);
                (void)entry_size;       // FIXME change protocol
                DBEntry<> e(ser);

                add_entry(move(e));
            }

        }

        static vector<DBEntry<>> deserialize_entries(const char* ser) {
            vector<DBEntry<>> ret;

            size_t num_entries = *(const size_t*)ser; ser += sizeof(size_t);

            for (size_t i = 0; i < num_entries; i++) {
                size_t entry_size = *(const size_t*)ser; ser += sizeof(size_t);
                (void)entry_size;       // FIXME change protocol
                DBEntry<> e(ser);
                ret.push_back(move(e));
            }

            return ret;
        }

};

}

extern "C" {
static void s_SeqDB_new_entry(const fn* s_this, ...) {
    // Extract the arguments
    va_list args;
    va_start(args, s_this);
    const char* const* new_tags = va_arg(args, const char* const*);
    const char* new_value = va_arg(args, const char*);
    dbkey_t new_key = va_arg(args, dbkey_t);
    va_end(args);

    pando::SeqDB* db = (pando::SeqDB*)s_this->state;
    pando::DBEntry<> entry { new_tags, new_value, new_key };
    db->note_entry_out();
    db->note_bytes_out(entry.serialize_size());
    db->stage_add_entry(move(entry));
}

static void s_SeqDB_get_entry_by_tags(const fn* s_this, ...) {
    // Extract the arguments
    va_list args;
    va_start(args, s_this);
    const char* const* search_tags = va_arg(args, const char* const*);
    char** res = va_arg(args, char**);
    va_end(args);

    pando::SeqDB* db = (pando::SeqDB*)s_this->state;
    db->get_entry_value_by_tags(search_tags, res);
}

static void s_SeqDB_get_entries_by_keys(const fn* s_this, ...) {
    // Arguments: size_t n, const dbkey_t* keys, char** values
    va_list args;
    va_start(args, s_this);
    size_t n = va_arg(args, size_t);
    const dbkey_t* keys = va_arg(args, const dbkey_t*);
    char** values = va_arg(args, char**);
    va_end(args);

    pando::SeqDB* db = (pando::SeqDB*)s_this->state;
    db->get_entry_values_by_keys(n, keys, values);
}

static void s_SeqDB_get_entry_by_key(const fn* s_this, ...) {
    // Extract the arguments
    va_list args;
    va_start(args, s_this);
    dbkey_t search_key = va_arg(args, dbkey_t);
    char** res = va_arg(args, char**);
    va_end(args);

    pando::SeqDB* db = (pando::SeqDB*)s_this->state;
    db->get_entry_value_by_key(search_key, res);

}

static void s_SeqDB_get_entries_by_tags(const fn* s_this, ...) {
    va_list args;
    va_start(args, s_this);
    const char* const* search_tags = va_arg(args, const char* const*);
    const DBAccess* access = va_arg(args, const DBAccess*);
    db_query_t callback = va_arg(args, db_query_t);
    va_end(args);
    pando::SeqDB* db = (pando::SeqDB*)s_this->state;
    db->get_entries_by_tags(search_tags, access, callback);
}

static void s_SeqDB_add_tag(const fn* s_this, ...) {
    va_list args;
    va_start(args, s_this);
    const char* new_tag = va_arg(args, const char*);
    va_end(args);

    // Note: consider changing this somehow to be less coupled to
    // make_new_entry
    DBAccess* acc = (DBAccess*)s_this->state;
    // Get the SeqDB from new_entry, the key from the key field
    pando::SeqDB* db = (pando::SeqDB*)acc->make_new_entry.state;
    db->note_tag_added();
    db->note_bytes_out(sizeof(dbkey_t) + strlen(new_tag));
    db->stage_add_tag(acc->key, new_tag);
}

static void s_SeqDB_remove_tag(const fn* s_this, ...) {
    va_list args;
    va_start(args, s_this);
    dbkey_t key = va_arg(args, dbkey_t);
    const char* old_tag = va_arg(args, const char*);
    va_end(args);

    // Note: consider changing this somehow to be less coupled to
    // make_new_entry
    DBAccess* acc = (DBAccess*)s_this->state;
    // Get the SeqDB from new_entry, the key from the key field
    pando::SeqDB* db = (pando::SeqDB*)acc->make_new_entry.state;
    db->note_tag_removed();
    db->note_bytes_out(sizeof(dbkey_t) + strlen(old_tag));
    db->stage_remove_tag(key, old_tag);
}

static void s_SeqDB_subscribe_to_entry(const fn* s_this, ...) {
    va_list args;
    va_start(args, s_this);
    dbkey_t my_key = va_arg(args, dbkey_t);
    dbkey_t wait_key = va_arg(args, dbkey_t);
    string tag  = va_arg(args, const char*);
    va_end(args);

    // Note: consider changing this somehow to be less coupled to
    // make_new_entry
    DBAccess* acc = (DBAccess*)s_this->state;
    // Get the SeqDB from new_entry, the key from the key field
    pando::SeqDB* db = (pando::SeqDB*)acc->make_new_entry.state;
    db->note_bytes_out(2*sizeof(dbkey_t) + tag.size());
    db->subscribe_to_entry_wrapper(my_key, wait_key, tag);
}

static void s_SeqDB_update_entry_val(const fn* s_this, ...) {
    va_list args;
    va_start(args, s_this);
    dbkey_t key = va_arg(args, dbkey_t);
    char* new_val_cstr = va_arg(args, char*);
    string new_val {new_val_cstr};
    DBAccess* acc = (DBAccess*)s_this->state;
    pando::SeqDB* db = (pando::SeqDB*)acc->make_new_entry.state;
    db->note_val_update();
    db->note_bytes_out(sizeof(dbkey_t) + new_val.size());
    db->stage_update_entry_val(key, new_val);
}

}
