#pragma once

#include "alg_db.hpp"

namespace pando {

typedef void* fn_ref;
typedef void (*fn_add_entry)(void*, DBEntry<>);
typedef void (*fn_add_tag_to_entry)(void*, dbkey_t, string);
typedef void (*fn_remove_tag_from_entry)(void*, dbkey_t, string);
typedef void (*fn_update_entry_val)(void*, dbkey_t, string);
typedef void (*fn_subscribe_to_entry)(void*, dbkey_t, dbkey_t, string);
typedef void (*fn_get_entry_by_key)(void*, dbkey_t, char**);
typedef void (*fn_get_entries_by_keys)(void*, size_t, const dbkey_t*, char**);

typedef struct fn_refs {
    fn_ref ref;
    fn_add_entry add_entry;
    fn_add_tag_to_entry add_tag_to_entry;
    fn_remove_tag_from_entry remove_tag_from_entry;
    fn_update_entry_val update_entry_val;
    fn_subscribe_to_entry subscribe_to_entry;
    fn_get_entry_by_key get_entry_by_key;
    fn_get_entries_by_keys get_entries_by_keys;

    fn_refs(
        fn_ref ref,
        fn_add_entry add_entry,
        fn_add_tag_to_entry add_tag_to_entry,
        fn_remove_tag_from_entry remove_tag_from_entry,
        fn_update_entry_val update_entry_val,
        fn_subscribe_to_entry subscribe_to_entry,
        fn_get_entry_by_key get_entry_by_key,
        fn_get_entries_by_keys get_entries_by_keys
    ) :
        ref(ref),
        add_entry(add_entry),
        add_tag_to_entry(add_tag_to_entry),
        remove_tag_from_entry(remove_tag_from_entry),
        update_entry_val(update_entry_val),
        subscribe_to_entry(subscribe_to_entry),
        get_entry_by_key(get_entry_by_key),
        get_entries_by_keys(get_entries_by_keys)
        { }
} fn_refs;

/** @brief Create a sequential DB but with parallel filter access controls */
class SeqDBParAccess : public AlgDB {
    private:
        fn_refs b_refs_;
    public:
        SeqDBParAccess(fn_refs refs, addr_t addr, size_t sz) : AlgDB(addr, sz), b_refs_(refs) { }

        virtual bool process() {
            return run_filters_();
        }

        virtual void stage_close() {
            // Add new entries
            for (auto & [key, entry] : new_entries_) {
                b_refs_.add_entry(b_refs_.ref, move(entry));
            }
            new_entries_.clear();

            // Update entry values
            if (val_updates_.size() != 0) {
                for (auto & [key, new_val] : val_updates_) {
                    b_refs_.update_entry_val(b_refs_.ref, key, new_val);
                }
            }
            val_updates_.clear();

            // Add tags
            for (auto & [key, tag] : tags_to_add_) {
                b_refs_.add_tag_to_entry(b_refs_.ref, key, tag);
            }
            tags_to_add_.clear();

            // Remove tags
            for (auto & [key, tag] : tags_to_remove_)
               b_refs_.remove_tag_from_entry(b_refs_.ref, key, tag);
            tags_to_remove_.clear();
        }

        /** @brief Set a tag to be removed from an entry later */
        virtual void stage_remove_tag(dbkey_t key, string tag) {
            b_refs_.remove_tag_from_entry(b_refs_.ref, key, tag);
        }

        virtual void stage_add_entry(DBEntry<> entry) {
            b_refs_.add_entry(b_refs_.ref, move(entry));
        }

        virtual void stage_add_tag(dbkey_t key, string tag) {
            b_refs_.add_tag_to_entry(b_refs_.ref, key, tag);
        }

        virtual void stage_update_entry_val(dbkey_t key, string new_val) {
            b_refs_.update_entry_val(b_refs_.ref, key, new_val);
        }

        /** @brief Get the value of the entry that has key search_key */
        virtual void get_entry_value_by_key(dbkey_t search_key, char **res) {
            b_refs_.get_entry_by_key(b_refs_.ref, search_key, res);
        }

        /** @brief Get the values for many keys, from whichever agents own them */
        virtual void get_entry_values_by_keys(size_t n, const dbkey_t* keys, char** values) {
            b_refs_.get_entries_by_keys(b_refs_.ref, n, keys, values);
        }

        virtual void subscribe_to_entry_wrapper(dbkey_t my_key, dbkey_t wait_key, string tag) {
            b_refs_.subscribe_to_entry(b_refs_.ref, my_key, wait_key, tag);
        }

        void add_entry(DBEntry<> entry) {
            b_refs_.add_entry(b_refs_.ref, move(entry));
        }

};

}
