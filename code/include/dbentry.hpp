#pragma once

#include <unordered_set>
#include <string_view>
#include <string>
#include <cstdarg>

#include <random>

#include "types.h"
#include "filter.hpp"
#include "dbkey.h"
#include "tag_table.hpp"

using namespace std;

namespace pando {

/** @brief Hold the database entries
 *
 * Tags are kept in a TagStore: by default as ids into the process's
 * TagTable (see tag_table.hpp), so entries with the same tags share one copy
 * of each tag string.
 */
template<typename Alloc=allocator<char>>
class DBEntry {
    private:
        using Str = basic_string<char, char_traits<char>, Alloc>;
        /** @brief A default allocator */
        std::allocator<char> def_alloc_;

        /** @brief Holds the allocator */
        Alloc& alloc_;

        /** @brief Hold the tags for the entry */
        TagStore tags_;

        /** @brief The tags as a C-string array ending in "", built when first
         * asked for (c_tags()) and dropped whenever the tags change */
        mutable const char** c_tags_ = nullptr;

        /** @brief Holds the value for the entry */
        Str value_;

        /** @brief Holds the key for the entry */
        dbkey_t key_;

        /** @brief False once the entry has been moved from or destroyed */
        bool valid_ = true;

        void check_valid_() const {
            if (!valid_) throw runtime_error("Invalid DBEntry");
        }

        /** @brief Drop the C tags (they are rebuilt when next asked for) */
        void drop_c_tags_() const {
            delete [] c_tags_;
            c_tags_ = nullptr;
        }

    public:
        /** @brief Construct a DB entry from C tags and values */
        DBEntry(Alloc& alloc, const char* const* tags, const char* value) :
                alloc_(alloc), value_(alloc_), key_(INITIAL_KEY) {
            clear();
            while (*tags[0] != '\0')
                tags_.insert(*tags++);
            value_.assign(value);
        }
        DBEntry(const char* const* tags, const char* value) : DBEntry(def_alloc_, tags, value) { }

        /** @brief Construct a DB entry from C tags, value, and key */
        DBEntry(Alloc& alloc, const char* const* tags, const char* value, dbkey_t key) :
                alloc_(alloc), value_(alloc_), key_(key) {
            while (*tags[0] != '\0')
                tags_.insert(*tags++);
            value_.assign(value);
        }
        DBEntry(const char* const* tags, const char* value, dbkey_t key) : DBEntry(def_alloc_, tags, value, key) { }

        DBEntry(Alloc& alloc) : alloc_(alloc), value_(alloc_), key_(INITIAL_KEY) { clear(); };
        DBEntry() : DBEntry(def_alloc_) { }

        ~DBEntry() { clear(true); }

        /** @brief Construct a DB entry from a move */
        DBEntry(DBEntry&& other) noexcept :
                alloc_(other.alloc_),
                tags_(move(other.tags_)),
                value_(alloc_),
                key_(move(other.key_)) {
            value_ = Str{alloc_};
            value_.assign(other.value_);        // FIXME can we avoid copying?

            // Invalidate the other
            other.clear(true);
        }
        /** @brief Move a DB entry */
        DBEntry& operator=(DBEntry&& other) noexcept {
            // Check if we are different from the move target
            if (&other != this) {
                clear(true);

                alloc_ = move(other.alloc_);
                tags_ = move(other.tags_);
                value_ = Str{alloc_};
                value_.assign(other.value_);        // FIXME can we avoid copying?
                key_ = move(other.key_);
                valid_ = true;

                other.clear(true);
            }

            return *this;
        }

        /** @brief Construct a DB entry from a copy */
        DBEntry(const DBEntry& other) :
                alloc_(other.alloc_),
                tags_(other.tags_),
                value_(other.value_),
                key_(INITIAL_KEY) { }
        /** @brief Copy a DB entry */
        DBEntry& operator=(const DBEntry& other) {
            if (&other != this) {
                alloc_ = other.alloc_;
                tags_ = other.tags_;
                value_ = other.value_;

                key_ = INITIAL_KEY;
                valid_ = true;
                drop_c_tags_();
            }

            return *this;
        }

        /** @brief Test if two DBEntry objects are not equal */
        bool operator!=(const DBEntry& other) {
            if (other.get_key() != get_key())
                return true;
            if (!other.tags_.equals(tags_))
                return true;
            if (other.value() != value())
                return true;
            return false;
        }
        /** @brief Test if two DBEntry objects are equal */
        bool operator==(const DBEntry& other) {
            return !(operator!=(other));
        }

        /** @brief Compute the size required for serialization
         *
         * @param with_value if false, the value is serialized as empty (only
         * the key and tags are kept)
         */
        size_t serialize_size(bool with_value=true) const {
            // Format:
            // <key>
            // <size(value)>
            // <value>
            // <num(tags)>
            // <foreach tag:
            //      <size(tag)>
            //      <tag>
            // >
            size_t ser_size = 0;

            ser_size += sizeof(dbkey_t);

            ser_size += sizeof(size_t);
            size_t value_size = with_value ? value_.size() : 0;
            ser_size += value_size;

            ser_size += sizeof(size_t);
            tags_.for_each([&](const string& tag) {
                ser_size += sizeof(size_t) + tag.size();
            });

            return ser_size;
        }

        /** @brief Place the serialized version of the object into an
         * already-allocated memory location
         *
         * @param with_value if false, the value is serialized as empty (only
         * the key and tags are kept)
         */
        void serialize(char*& ser_ptr, bool with_value=true) const {
            size_t value_size = with_value ? value_.size() : 0;

            // Copy in the data
            *(dbkey_t*)ser_ptr = key_; ser_ptr += sizeof(dbkey_t);
            *(size_t*)ser_ptr = value_size; ser_ptr += sizeof(size_t);
            memcpy(ser_ptr, value_.c_str(), value_size); ser_ptr += value_size;

            *(size_t*)ser_ptr = tags_.size(); ser_ptr += sizeof(size_t);
            tags_.for_each([&](const string& tag) {
                size_t tag_size = tag.size();
                *(size_t*)ser_ptr = tag_size; ser_ptr += sizeof(size_t);
                memcpy(ser_ptr, tag.c_str(), tag_size); ser_ptr += tag_size;
            });
        }

        /** @brief Initialize from serialized data */
        DBEntry(Alloc& alloc, const char*& ser) :
                alloc_(alloc), value_(alloc), key_(INITIAL_KEY) {
            clear();

            // Copy out the key
            key_ = *(const dbkey_t*)ser; ser += sizeof(dbkey_t);

            // Copy the value
            size_t val_size = *(const size_t*)ser; ser += sizeof(size_t);
            value_.assign(ser, ser+val_size); ser += val_size;

            size_t ntags = *(const size_t*)ser; ser += sizeof(size_t);
            for (; ntags > 0; --ntags) {
                size_t tag_size = *(const size_t*)ser; ser += sizeof(size_t);
                tags_.insert(string_view {ser, tag_size}); ser += tag_size;
            }
        }
        DBEntry(const char*& ser) : DBEntry(def_alloc_, ser) { }

        /** @brief Clear all memory in the entry
         *
         * This resets the tags and values, returning the entry to an original
         * state
         *
         * @param f if true, also reset the key and mark the entry invalid (as
         * after a move or when destroyed)
         */
        DBEntry& clear(bool f=false) {
            tags_.clear();
            value_.assign("");
            drop_c_tags_();

            if (f)
                key_ = INITIAL_KEY;
            valid_ = !f;
            return *this;
        }

        /** @brief Add a (potentially) new tag 
         * NOTE: This is redundant with the add_tag method, but makes python
         * bindings easier
         * */
        DBEntry& add_single_tag(string tag) {
            return add_tag(tag);
        }

        /** @brief Add a (potentially) new tag */
        DBEntry& add_tag(string tag) {
            check_valid_();
            tags_.insert(tag);
            drop_c_tags_();
            return *this;
        }

        /** @brief Add multiple tags */
        template<typename ...Args>
        DBEntry& add_tag(string tag, Args && ...args) {
            add_tag(tag);
            return add_tag(args...);
        }

        /** @brief Remove a tag */
        DBEntry& remove_tag(string tag) {
            tags_.erase(tag);
            drop_c_tags_();
            return *this;
        }

        /** @brief Remove multiple tags */
        template<typename ...Args>
        DBEntry& remove_tag(string tag, Args && ...args) {
            remove_tag(tag);
            return remove_tag(args...);
        }

        /** @brief Add more to the value string */
        DBEntry& add_to_value(const string& more) {
            check_valid_();
            value_ += more + '\n';
            return *this;
        }

        /** @brief Return the number of tags */
        size_t tag_size() const {
            check_valid_();
            return tags_.size();
        }

        /** @brief Check whether a tag exists */
        bool has_tag(string check) const {
            check_valid_();
            return tags_.contains(check);
        }

        /** @brief Return the C-style tags
         *
         * This contains a C-string for each tag, followed by an empty C-string.
         * It stays valid until the entry's tags change or the entry goes away.
         */
        const char* const* c_tags() const {
            check_valid_();
            if (c_tags_ == nullptr) {
                size_t n = tags_.size();
                const char** arr = new const char*[n+1];
                size_t i = 0;
                tags_.for_each([&](const string& tag) { arr[i++] = tag.c_str(); });
                arr[n] = "";
                c_tags_ = arr;
            }
            return c_tags_;
        }

        /** @brief Return the tags (a copy, as a set of strings) */
        unordered_set<string> tags() const { return tags_.to_set(); }

        /** @brief The tags as stored */
        const TagStore& tag_store() const { return tags_; }

        /** @brief Replace this entry's tags with another entry's (of any
         * allocator), without going through strings when both are interned */
        template<typename OAlloc>
        DBEntry& copy_tags_from(const DBEntry<OAlloc>& other) {
            tags_ = other.tag_store();
            drop_c_tags_();
            return *this;
        }

        /** @brief Return whether the entry is valid (not moved from or destroyed) */
        bool valid_c_tags() const { return valid_; }

        /** @brief Return the value as a const string ref */
        const Str& value() const {
            check_valid_();
            return value_;
        }
        /** @brief Return a mutable value */
        Str& value() {
            check_valid_();
            return value_;
        }

        /** @brief Set the value to a string value */
        void set_value(const string& val) {
            value() = val;
        }

        /** @brief Return access function pointers to this entry */
        DBAccess* access() {
            check_valid_();
            DBAccess* ret = new DBAccess;

            // Set the value, c_tags, and key
            ret->tags = c_tags();
            ret->value = value_.c_str();
            ret->key = key_;
            return ret;
        }

        /** @brief Set the key on the entry */
        DBEntry& set_key(dbkey_t k) {
            key_ = k;
            return *this;
        }
        /** @brief Return the key for the entry */
        const dbkey_t& get_key() const { return key_; }

        /** @brief Return whether the key is random or not */
        bool random_key() const { return is_random_key(key_); }

        template<typename OAlloc>
        friend ostream &operator<<(ostream &os, pando::DBEntry<OAlloc> const &entry);
};

ostream &operator<<(ostream &os, dbkey_t const &k) {
    os << k.a << ":" << k.b << ":" << k.c;
    return os;
}

template<typename Alloc=allocator<char>>
ostream &operator<<(ostream &os, DBEntry<Alloc> const &entry) {
    os << "KEY:" << endl;
    if (entry.random_key())
        os << "(random)" << endl;
    else
        os << entry.key_ << endl;
    os << "TAGS:" << endl;
    entry.tags_.for_each([&](const string& t) { os << t << endl; });
    os << "VALUE:" << endl;
    os << entry.value();
    return os;
}

}
