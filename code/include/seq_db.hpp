#pragma once

#include <unordered_map>
#include <filesystem>
#include <exception>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <iterator>
#include <algorithm>
#include <chrono>
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

using namespace std;
using namespace std::chrono;

extern "C" {
static void s_SeqDB_new_entry(const fn* s_this, ...);
static void s_SeqDB_get_entry_by_tags(const fn* s_this, ...);
static void s_SeqDB_get_entry_by_key(const fn* s_this, ...);
static void s_SeqDB_get_entries_by_tags(const fn* s_this, ...);
static void s_SeqDB_add_tag(const fn* s_this, ...);
static void s_SeqDB_remove_tag(const fn* s_this, ...);
static void s_SeqDB_subscribe_to_entry(const fn* s_this, ...);
static void s_SeqDB_update_entry_val(const fn* s_this, ...);
}

typedef int (*db_query_t)(dbkey_t, const char*, const char* const*, const DBAccess*);

namespace pando {

static string MERGE_STRATEGY_FORCE = "MERGE_STRATEGY=FORCE_MERGE";

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

        bool run_filters_() {
            bool filter_ran = false;
            auto keys = db_.keys();
            size_t i = 0;
            size_t num_keys [[maybe_unused]]  = keys.size(); // unused w/o verbosity

            // limit how many filters we run before a stage close to help with memory usage
            size_t MAX_FILTERS_TO_RUN = 100000;
            size_t num_filters_run = 0;


            // Iterate through the database
            for (auto & key : keys) {         // TODO : replace this with a C++ iterator that automatically "batches" behind the scene
                auto entry = db_.retrieve(key); //TODO also batch retrievals
                ++i;
                #ifdef VERBOSE
                if (i % 10000 == 0) {
                    cerr << "I have processed " << i << "/" <<  num_keys << " entries and run " << num_filters_run << " filters"  << endl;
                }
                #endif
                // @TODO profile to determine if this needs to be optimized out.
                //       this costs (|F|*|E|)
                for (auto & [filter_name, filter] : installed_filters_) {
                    // Only run on filters that are SINGLE_ENTRY filters
                    if (filter->filter_type() != SINGLE_ENTRY) {
                        continue;
                    }

                    // @TODO move back out; make a test for add tags and then
                    // get tags inside of a filter
                    // Get the DB access -- need to redo this for each filter,
                    // can't be pulled out of the loop because things can
                    // change between filter runs
                    DBAccess* access = entry.access();
                    add_db_access(access);

                    if (filter->should_run(access)) {
                        filter_ran = true;
                        filter->run(access);
                        ++num_filters_run;
                    }

                    delete access;
                }

                if (num_filters_run > MAX_FILTERS_TO_RUN) {
                    #ifdef VERBOSE
                    cerr << "breaking early due to max filter run limit" << endl;
                    #endif
                    break;
                }
            }

            #ifdef VERBOSE
            cerr << "done processing" << endl;
            #endif

            return filter_ran;
        }


    public:

        /** @brief Create a database with no initial state
         * NOTE: localhost IP addresses used as default values for test cases
         * */
        SeqDB() : db_server_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}),
            db_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}, ZMQAddress {"127.0.0.1", 0}) { Py_Initialize(); }

        /** @brief Create a database with an agent id to use as seed */
        SeqDB(ZMQAddress addr) : db_server_(addr+MAP_LOCALNUM_OFFSET), db_(addr+MAP_LOCALNUM_OFFSET, addr) {
            set_agent_id(addr.serialize());
            Py_Initialize();
        }

        /** @brief Create a database with an agent id to use as seed and allocator size*/
        SeqDB(ZMQAddress addr, size_t size) : db_server_(addr+MAP_LOCALNUM_OFFSET, size), db_(addr+MAP_LOCALNUM_OFFSET, addr) {
            set_agent_id(addr.serialize());
            Py_Initialize();
        }

        /** @brief Build a SeqDB from an input file */
        SeqDB(string fn) : db_server_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}),
                db_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}, ZMQAddress {"127.0.0.1", 0}) {
            add_db_file(fn);

            Py_Initialize();
        }

        ~SeqDB() {
            Py_FinalizeEx();
        }

        /** @brief Build a SeqDB from an input file with a DB size */
        SeqDB(string fn, size_t size) : db_server_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}, size),
                                        db_(ZMQAddress {"127.0.0.1", 0+MAP_LOCALNUM_OFFSET}, ZMQAddress {"127.0.0.1", 0}) {
            add_db_file(fn);
            Py_Initialize();
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
            for (auto &tag : entry->tags()) {
                add_to_tag_index_(key, tag);
            }
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

                if (should_concat)
                    *entry = concat_entries(&e, entry, force_merge);
            }
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

        DBEntry<> concat_entries(DBEntry<>* e1, DBEntry<>* e2, bool force_merge=false) {
            DBEntry<> new_entry;
            new_entry.value() = e1->value() + '\n' + e2->value();

            #ifdef VERBOSE
            cerr << "MERGING entries, new size = " << new_entry.value().size() << endl;
            #endif
            //Assume e1 and e2 have the same key
            dbkey_t new_key = e1->get_key();
            new_entry.set_key(new_key);

            vector<string> tag_union;
            if (force_merge) {
                // if we are force merging, assume they have the same tags TODO is this okay? It might be an optimization
                auto & e1_tags = e1->tags();
                tag_union.assign(e1_tags.begin(), e1_tags.end());
            } else {
                auto e1_tags = e1->tags();
                auto e2_tags = e2->tags();
                set_union(e1_tags.begin(), e1_tags.end(), e2_tags.begin(), e2_tags.end(), inserter(tag_union, tag_union.begin()));
            }
            tag_union.push_back("MERGED");
            for (auto &tag : tag_union) {
                new_entry.add_tag(tag);
            }

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

                if (should_concat) {
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
        }

        /** @brief Process one iteration, returning whether any filters were ran */
        bool process_once() {
            // Close any pending changes
            bool filter_ran = run_filters_();
            stage_close();
            return filter_ran;
        }

        /** @brief Process the database, applying filters as appropriate */
        virtual bool process() {
            while (process_once()) {}
            return false;
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
            auto keys = get_entry_by_tags(tags);

            size_t ssize = serialize_size(keys);

            pigo::WFile out_f {export_fn, ssize};

            char* data_ptr = (char*)out_f.fp();

            serialize_entries(data_ptr, keys);
        }

        void import_db(string import_fn) {
            pigo::ROFile in_f {import_fn};
            const char* data_ptr = in_f.fp();

            deserialize_and_add_entries(data_ptr);

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
    db->stage_update_entry_val(key, new_val);
}

}
