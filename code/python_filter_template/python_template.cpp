#include <iostream>
#include "filter.hpp"
#include <string>

#include "@filter_name@.h"

using namespace std;
using namespace pando;

static const char* filter_name = "@filter_name@";
static const char* cython_fail_tag = "@filter_name@" ":cython_fail";

static void ensure_python_module_initialized() {
    PyObject* sys_modules = PyImport_GetModuleDict();
    if (sys_modules != NULL && PyDict_GetItemString(sys_modules, filter_name) != NULL) {
        return;
    }
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    PyInit_@filter_name@();
    #pragma GCC diagnostic pop
}

// Fit the Pando API
extern "C" {
    void run_(const DBAccess *access) {
        ensure_python_module_initialized();
        
        PyObject* pmodule = PyImport_ImportModule(filter_name);
        if (!pmodule) {
            PyErr_Print();  // Print the error and clear the error. If you don't want it to be printed, replace with PyErr_Clear();
            fprintf(stderr, "WARNING: could not import module, might mean it was already imported\n");
            access->add_tag.run(&access->add_tag, cython_fail_tag); 
        }

        // Call the run function
        run_internal(access);

        if (PyErr_Occurred()) {
            PyErr_Print();  // Print the error and clear the error. If you don't want it to be printed, replace with PyErr_Clear();
            fprintf(stderr, "ERROR: Exception occurred in run_internal\n");
            access->add_tag.run(&access->add_tag, cython_fail_tag);
        }
    }
    /** @brief Main filter entry point */
    extern void run(void* access) {
        // Run internally
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        ensure_python_module_initialized();
        PyObject* pmodule = PyImport_ImportModule(filter_name);
        
        // Here we avoid the infinite loop by checking to see if the cython code crashed in some way and return false if it has
        auto tags = access->tags;
        while ((*tags)[0] != '\0') {
            if (string(*tags) == cython_fail_tag)
                return false;
            ++tags;
        }

        if (!pmodule) {
            PyErr_Print();  // Print the error and clear the error. If you don't want it to be printed, replace with PyErr_Clear();
            fprintf(stderr, "WARNING: could not import module, might mean it was already imported\n");
            access->add_tag.run(&access->add_tag, cython_fail_tag);
            return false;
        }

        bool should = should_run_internal(access);
        
        // We also check for a potential crash in cython's should_run function
        if (PyErr_Occurred()) {
            PyErr_Print();  // Print the error and clear the error. If you don't want it to be printed, replace with PyErr_Clear();
            fprintf(stderr, "ERROR: Exception occured in should_run_internal\n");
            access->add_tag.run(&access->add_tag, cython_fail_tag);
            return false;
        }

        return should;

    }

    /** @brief Contains the entry point and tags for the filter */
    extern const FilterInterface filter {
        filter_name: filter_name,
        filter_type: SINGLE_ENTRY,
        should_run: &should_run,
        init: nullptr,
        destroy: nullptr,
        run: &run
    };

}
