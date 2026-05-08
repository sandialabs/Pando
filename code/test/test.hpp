#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <exception>

#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>

/**
 * @file tests.hpp
 * @brief This file contains helper macros for running simple unit tests
 */

/** The floating point equivalence requirement */
#define F_EPS 1e-8

#define AEQ(x,y)                                                                         \
    do {                                                                                \
        if ((x) != (y)) {                                                               \
            std::cerr << "FAILURE: Expected " << (y) << " instead got " << (x)          \
                << " for " << #x << " in " << __FILE__ << ":" << __LINE__ << std::endl; \
            throw std::runtime_error("test failure");                                   \
            }                                                                           \
    } while(0);


#define EQ(x,y)                                                                         \
    do {                                                                                \
        if ((x) != (y)) {                                                               \
            std::cerr << "FAILURE: Expected " << (y) << " instead got " << (x)          \
                << " for " << #x << " in " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1;                                                                   \
            }                                                                           \
    } while(0);

#define TEST_FAIL                                           \
    do {                                                    \
        std::cerr << "FAILURE: Reached invalid point at "   \
            << __FILE__ << ":" << __LINE__ << std::endl;    \
        return 1;                                           \
    } while(0);

#define NOTEQ(x,y)                                                                      \
    do {                                                                                \
        if ((x) == (y)) {                                                               \
            std::cerr << "FAILURE: Expected !=" << (y) << " instead got " << (x)        \
                << " for " << #x << " in " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1;                                                                   \
            }                                                                           \
    } while(0);

#define NOPRINT_EQ(x,y)                                                                 \
    do {                                                                                \
        if ((x) != (y)) {                                                               \
            std::cerr << "FAILURE: values not equal"                                    \
                << " for " << #x << " in " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1;                                                                   \
            }                                                                           \
    } while(0);

#define NOPRINT_NOTEQ(x,y)                                                              \
    do {                                                                                \
        if ((x) == (y)) {                                                               \
            std::cerr << "FAILURE: values were equal"                                   \
                << " for " << #x << " in " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1;                                                                   \
            }                                                                           \
    } while(0);

#define FEQ(x,y)                                                                        \
    do {                                                                                \
        if (std::fabs((x) - (y)) > F_EPS) {                                             \
            std::cerr << "FAILURE: Expected " << (y) << " instead got " << (x)          \
                << " for " << #x << " in " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1;                                                                   \
            }                                                                           \
    } while(0);

#define TEST(f, ...) int f(                             \
        [[maybe_unused]] string build_dir,              \
        [[maybe_unused]] string src_dir,                \
        [[maybe_unused]] string data_dir, ##__VA_ARGS__)
#define TEST_PASS return 0;

#define RUN_TEST(f, ...)                                            \
    do {                                                            \
        int pass_;                                                  \
        try {                                                       \
            pass_ = f(build_dir, src_dir, data_dir, ##__VA_ARGS__); \
            if (pass_ != 0) {                                       \
                    std::cerr << "\e[1;36m✘ " << #f << " FAILED";   \
                } else {                                            \
                    std::cerr << "\e[32m✓ " << #f;                  \
                    ++test_pass;                                    \
                }                                                   \
        } catch (std::exception& e) {                               \
            std::cerr << "ABORT: " << e.what() << std::endl;        \
            std::cerr << "\e[1;36m✘ " << #f << " FAILED";           \
            pass_ = 1;                                              \
        } catch (...) {                                             \
            std::cerr << "ABORT!" << std::endl;                     \
            std::cerr << "\e[1;36m✘ " << #f << " FAILED";           \
            pass_ = 1;                                              \
        }                                                           \
        std::cerr << "\e[0m" << std::endl;                          \
        ++test_count;                                               \
        pass |= pass_;                                              \
    } while(0);

#define TESTS_MAIN int main(int argc, char **argv)
#define TESTS_BEGIN TESTS_MAIN {                                            \
    if (argc != 3) {                                                        \
        std::cout << "Usage: " << argv[0] << " build-dir test-src-dir" <<   \
                std::endl;                                                  \
        return EXIT_FAILURE;                                                \
    }                                                                       \
    {                                                                       \
        std::string _probe = "/scratch/_pando_test_probe";                  \
        if (mkdir(_probe.c_str(), 0700) != 0) {                             \
            std::cerr << "FATAL: /scratch not writable: "                   \
                << std::strerror(errno) << std::endl;                       \
            return EXIT_FAILURE;                                            \
        }                                                                   \
        rmdir(_probe.c_str());                                              \
    }                                                                       \
    string build_dir {argv[1]};                                             \
    string src_dir {argv[2]};                                               \
    string data_dir {src_dir+"/data"};                                      \
    size_t test_count = 0;                                                  \
    size_t test_pass = 0;                                                   \
    int pass = 0;
#define TESTS_END \
    std::cout << "TESTS FINISHED : " << test_pass << "/" << test_count <<   \
              " passed (" << 100*test_pass/test_count << "%)" << std::endl; \
    return pass; }
