#pragma once

#include <unordered_set>
#include <string>
#include <cstdarg>

#include <random>

#include "types.h"
#include "filter.hpp"
#include "dbkey.h"

using namespace std;

namespace pando {

/** @brief Hold the database entries */
template<typename Alloc=allocator<char>>
class DBEntry {
    private:
        using Str = basic_string<char, char_traits<char>, Alloc>;
        /** @brief A default allocator */
        std::allocator<char> def_alloc_;

        /** @brief Holds the allocator */
        Alloc& alloc_;

        /** @brief Hold the tags for the entry */
        unordered_set<string> tags_;

        /** @brief Hold the tags again as a c-string array */
        const char** c_tags_;

        /** @brief Holds the value for the entry */
        Str value_;

        /** @brief Holds a special empty string for terminating tag lists */
        const string empty_string_;

        /** @brief Holds the key for the entry */
        dbkey_t key_;

        /** @brief Update the internal c_tags
         *
         * @param f if true, delete but do not re-create tags
         */
        void update_c_tags_(bool f=false) {
            // First, delete all prior c_tags
            if (c_tags_ != nullptr) {
                // Do not worry about freeing any internal strings, those are
                // handled by tags_
                // Remove the full tags container
                delete [] c_tags_;
                c_tags_ = nullptr;
            }
            if (f) return;

            // Second, allocate new tags
            size_t num_tags = tags_.size();
            // Allocate space for the number of tags + 1
            c_tags_ = new const char*[num_tags+1];
            c_tags_[num_tags] = empty_string_.c_str();

            // Assign the c strings for each tag
            size_t tag_ctr = 0;
            for (const string & tag : tags_)
                    c_tags_[tag_ctr++] = tag.c_str();
        }


    public:
        /** @brief Construct a DB entry from C tags and values */
        DBEntry(Alloc& alloc, const char* const* tags, const char* value) :
                alloc_(alloc), c_tags_(nullptr), value_(alloc_), key_(INITIAL_KEY) {
            clear();
            while (*tags[0] != '\0')
                tags_.insert(*tags++);
            value_.assign(value);
            update_c_tags_();
        }
        DBEntry(const char* const* tags, const char* value) : DBEntry(def_alloc_, tags, value) { }

        /** @brief Construct a DB entry from C tags, value, and key */
        DBEntry(Alloc& alloc, const char* const* tags, const char* value, dbkey_t key) :
                alloc_(alloc), c_tags_(nullptr), value_(alloc_), key_(key) {
            while (*tags[0] != '\0')
                tags_.insert(*tags++);
            value_.assign(value);
            update_c_tags_();
        }
        DBEntry(const char* const* tags, const char* value, dbkey_t key) : DBEntry(def_alloc_, tags, value, key) { }

        DBEntry(Alloc& alloc) : alloc_(alloc), c_tags_(nullptr), value_(alloc_), key_(INITIAL_KEY) { clear(); };
        DBEntry() : DBEntry(def_alloc_) { }

        ~DBEntry() { clear(true); }

        /** @brief Construct a DB entry from a move */
        DBEntry(DBEntry&& other) noexcept :
                alloc_(other.alloc_),
                tags_(move(other.tags_)),
                c_tags_(other.c_tags_),
                value_(alloc_),
                key_(move(other.key_)) {
            value_ = Str{alloc_};
            value_.assign(other.value_);        // FIXME can we avoid copying?

            // Invalidate the other
            other.c_tags_ = nullptr;
            other.clear(true);

            // Fix C tags if necessary
            update_c_tags_();
        }
        /** @brief Move a DB entry */
        DBEntry& operator=(DBEntry&& other) noexcept {
            // Check if we are different from the move target
            if (&other != this) {
                clear(true);

                alloc_ = move(other.alloc_);
                tags_ = move(other.tags_);
                c_tags_ = other.c_tags_;
                value_ = Str{alloc_};
                value_.assign(other.value_);        // FIXME can we avoid copying?
                key_ = move(other.key_);

                other.c_tags_ = nullptr;
                other.clear(true);

                update_c_tags_();
            }

            return *this;
        }

        /** @brief Construct a DB entry from a copy */
        DBEntry(const DBEntry& other) :
                alloc_(other.alloc_),
                tags_(other.tags_),
                c_tags_(nullptr),
                value_(other.value_),
                key_(INITIAL_KEY) {
            // Build the C tags
            update_c_tags_();
        }
        /** @brief Copy a DB entry */
        DBEntry& operator=(const DBEntry& other) {
            if (&other != this) {
                alloc_ = other.alloc_;
                tags_ = other.tags_;
                value_ = other.value_;

                key_ = INITIAL_KEY;

                // Build the C tags
                update_c_tags_();
            }

            return *this;
        }

        /** @brief Test if two DBEntry objects are not equal */
        bool operator!=(const DBEntry& other) {
            if (other.get_key() != get_key())
                return true;
            if (other.tags() != tags())
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
            for (const string& tag : tags_) {
                ser_size += sizeof(size_t);
                size_t tag_size = tag.size();
                ser_size += tag_size;
            }

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
            for (const string& tag : tags_) {
                size_t tag_size = tag.size();
                *(size_t*)ser_ptr = tag_size; ser_ptr += sizeof(size_t);
                memcpy(ser_ptr, tag.c_str(), tag_size); ser_ptr += tag_size;
            }
        }

        /** @brief Initialize from serialized data */
        DBEntry(Alloc& alloc, const char*& ser) :
                alloc_(alloc), c_tags_(nullptr), value_(alloc), key_(INITIAL_KEY) {
            clear();

            // Copy out the key
            key_ = *(const dbkey_t*)ser; ser += sizeof(dbkey_t);

            // Copy the value
            size_t val_size = *(const size_t*)ser; ser += sizeof(size_t);
            value_.assign(ser, ser+val_size); ser += val_size;

            size_t ntags = *(const size_t*)ser; ser += sizeof(size_t);
            for (; ntags > 0; --ntags) {
                string tag;
                size_t tag_size = *(const size_t*)ser; ser += sizeof(size_t);
                tag.assign(ser, ser+tag_size); ser += tag_size;
                tags_.insert(tag);
            }

            update_c_tags_();
        }
        DBEntry(const char*& ser) : DBEntry(def_alloc_, ser) { }

        /** @brief Clear all memory in the entry
         *
         * This resets the tags and values, returning the entry to an original
         * state
         *
         * This will also set the key to a new, random key
         *
         * @param f if true, remove all memory from the c_tags_, invaliding it
         */
        DBEntry& clear(bool f=false) {
            tags_.clear();
            value_.assign("");

            if (f)
                key_ = INITIAL_KEY;

            update_c_tags_(f);
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
            if (c_tags_ == nullptr) throw runtime_error("Invalid DBEntry");
            tags_.insert(tag);
            update_c_tags_();
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
            update_c_tags_();
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
            if (c_tags_ == nullptr) throw runtime_error("Invalid DBEntry");
            value_ += more + '\n';
            return *this;
        }

        /** @brief Return the number of tags */
        size_t tag_size() const {
            if (c_tags_ == nullptr) throw runtime_error("Invalid DBEntry");
            return tags_.size();
        }

        /** @brief Check whether a tag exists */
        bool has_tag(string check) const {
            if (c_tags_ == nullptr) throw runtime_error("Invalid DBEntry");
            return tags_.count(check) > 0;
        }

        /** @brief Return the C-style tags
         *
         * This contains a C-string for each tag, followed by an empty C-string
         */
        const char* const* c_tags() const {
            if (c_tags_ == nullptr) throw runtime_error("Invalid DBEntry");
            return c_tags_;
        }

        /** @brief Return the tags */
        const unordered_set<string> & tags() const {return tags_;}

        /** @brief Return whether C-tags is valid */
        bool valid_c_tags() const { return c_tags_ != nullptr; }

        /** @brief Return the value as a const string ref */
        const Str& value() const {
            if (c_tags_ == nullptr) throw runtime_error("Invalid DBEntry");
            return value_;
        }
        /** @brief Return a mutable value */
        Str& value() {
            if (c_tags_ == nullptr) throw runtime_error("Invalid DBEntry");
            return value_;
        }

        /** @brief Set the value to a string value */
        void set_value(const string& val) {
            value() = val;
        }

        /** @brief Return access function pointers to this entry */
        DBAccess* access() {
            if (c_tags_ == nullptr) throw runtime_error("Invalid DBEntry");
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
    for (auto & t : entry.tags_)
        os << t << endl;
    os << "VALUE:" << endl;
    os << entry.value();
    return os;
}

}
