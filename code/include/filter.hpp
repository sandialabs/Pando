#pragma once

#include "alg_access.hpp"
#include "dbkey.h"

#include <dlfcn.h>

#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include <exception>
#include <Python.h>

using namespace std;

extern "C" {

/** @brief Contains access information for a filter back to the DB */
typedef struct fn fn;
struct fn {
    void* state;
    /** Run will append exactly its arguments to the journal */
    /** When processing later, it will run:
     *  fn->run_(fn, <args>)
     */
    void (*run)(const fn*, ...);
    void (*run_)(const fn*, ...);
};
typedef struct DBAccess {
    fn make_new_entry;
    fn get_entry_by_tags;
    fn get_entry_by_key;
    fn remove_tag_by_tags;
    fn get_entries_by_tags;
    fn subscribe_to_entry;
    fn update_entry_val;
    dbkey_t key;
    /** Members specific for SINGLE_ENTRY filters */
    fn add_tag;
    fn remove_tag;
    const char* const* tags;
    const char* value;
    /** Members specific for GROUP_ENTRIES filters */
    GroupAccess* group;
    /** Look up many keys with one blocking request:
     *    run(fn*, size_t n, const dbkey_t* keys, char** values)
     *  values[i] is set to a malloc'd, NUL-terminated copy of the value for
     *  keys[i] (free it), or NULL if the key does not exist. The keys are
     *  sent to all of their owning agents at once.
     *  (Kept last so filters built before it was added still work.) */
    fn get_entries_by_keys;
} DBAccess;

enum filter_type {
    SINGLE_ENTRY,
    GROUP_ENTRIES
};

/** @brief Holds the tags and entry point for a filter */
typedef struct FilterInterface {
    const char* filter_name;
    enum filter_type filter_type;
    bool (*should_run)(const DBAccess*);
    void* (*init)(DBAccess*);
    void (*destroy)(void*);
    void (*run)(void*);
} FilterInterface;

}

namespace pando {

/** @brief Runs the internal filter as appropriate */
class Filter {
    private:
        /** @brief Holds the dlopen filter object */
        void* obj_;
        const FilterInterface* i_;

    public:
        /** @brief Construct from a file */
        Filter(string fn) {
            // Open the filter
            obj_ = dlopen(fn.c_str(), RTLD_LAZY);
            if (obj_ == nullptr) {
                fprintf(stderr, "dlopen failed: %s\n", dlerror());
                throw runtime_error("Unable to open filter");
            }

            // Find the filter struct
            i_ = (const FilterInterface*)dlsym(obj_, "filter");
            if (i_ == nullptr)
                throw runtime_error("Unable to find the filter object in .so file " + fn);
        }

        /** @brief Build a filter from in-memory */
        Filter(const FilterInterface *i) :
            obj_(nullptr), i_(i) {}

        /** @brief Create an empty filter */
        Filter() : obj_(nullptr), i_(nullptr) { }

        /** @brief Close the dynamic library on destruction */
        ~Filter() { if (obj_ != nullptr) dlclose(obj_); }

        /** @brief Prevent copying */
        Filter(const Filter& other) = delete;
        /** @brief Prevent copying */
        Filter& operator=(const Filter& other) = delete;

        /** @brief Support moving via constructors */
        Filter(Filter&& other) :
            obj_(other.obj_), i_(other.i_)
        {
            other.obj_ = nullptr;
            other.i_ = nullptr;
        }
        /** @brief Support moving via assignment */
        Filter& operator=(Filter&& other) {
            if (&other != this) {
                obj_ = other.obj_;
                i_ = other.i_;

                other.obj_ = nullptr;
                other.i_ = nullptr;
            }
            return *this;
        }

        /** @brief Return whether the interface is valid */
        bool valid() {
            return (i_ != nullptr);
        }

        /** @brief Return the filter name */
        const char* name() {
            if (!valid()) throw runtime_error("Invalid filter");

            return i_->filter_name;
        }

        /** @brief Return a vector of tags from a given C-style tags */
        static vector<string> extract_c_tags(const char* const* tags) {
            vector<string> ret;
            while ((*tags)[0] != '\0') {
                ret.emplace_back(*tags++);
            }
            return ret;
        }

        bool should_run(const DBAccess* access) {
            return i_->should_run(access);
        }

        /** @brief Initialize a filter for running */
        void* init(DBAccess* access) {
            if (i_->init == nullptr) throw runtime_error("Filter does not support init");
            return i_->init(access);
        }

        /** @brief Run a filter with state */
        void run_with_state(void* state) {
            i_->run(state);
        }

        /** @brief Destroy a filter state */
        void destroy(void* state) {
            if (i_->destroy == nullptr) throw runtime_error("Filter does not support destroy");
            i_->destroy(state);
        }

        /** @brief Run the filter */
        void run(DBAccess* access) {
            if (i_->init == nullptr)
                return i_->run(access);

            auto state = init(access);
            run_with_state(state);
            destroy(state);
        }


        /** @brief Return the filter type */
        enum filter_type filter_type() { return i_->filter_type; }
};

using filter_p = shared_ptr<Filter>;

}
