#pragma once

#include <exception>
#include <cstring>
#include <cstdio>
#include <random>
#include <memory>
#include <string>
#include <array>
#include <mutex>
#include <cassert>

#ifndef PANDO_NO_BIGPIGO
#define CONFIG_BIGPIGO
#endif

#ifdef CONFIG_BIGPIGO
#define PIGO_SCRATCH "/scratch/"
#include "pigo.hpp"
#endif

using namespace std;

namespace pando {

class BigSpace {
    public:
        /** Requests up to this size use 16-byte-spaced size classes */
        static constexpr size_t SMALL_MAX = 1024;
        static constexpr size_t ALIGN = 16;
        static constexpr size_t NUM_SMALL_CLASSES = SMALL_MAX / ALIGN; // 64
        /** Above SMALL_MAX: 4 classes per power of two, for exponents 10..63 */
        static constexpr size_t NUM_CLASSES = NUM_SMALL_CLASSES + 4*(64-10);

        /** Index of the size class that holds a request of `bytes` bytes */
        static size_t size_class_index(size_t bytes) {
            if (bytes <= SMALL_MAX) {
                if (bytes == 0) return 0;
                return (bytes + ALIGN - 1) / ALIGN - 1;
            }
            // Classes have the form 2^b * {1.25, 1.5, 1.75, 2}, b >= 10
            size_t m = bytes - 1;
            size_t b = 63 - __builtin_clzll(m);
            size_t q = (m >> (b-2)) & 3;
            return NUM_SMALL_CLASSES + (b-10)*4 + q;
        }

        /** Size in bytes of every block in size class `idx` */
        static size_t class_size(size_t idx) {
            if (idx < NUM_SMALL_CLASSES)
                return (idx+1) * ALIGN;
            size_t i = idx - NUM_SMALL_CLASSES;
            size_t b = 10 + i/4;
            size_t q = i%4;
            return (5+q) << (b-2);
        }

    private:
        char* space_;
        char* end_;
        char* ptr_;
        const size_t size_;
        /** Heads of the per-class singly linked free lists; the next pointer
         * is stored in the first bytes of each free block */
        std::array<void*, NUM_CLASSES> free_heads_ {};
        std::mutex mtx_;
        #ifdef CONFIG_BIGPIGO
        pigo::WFile* file_;
        string fname_;
        string dname_;
        #endif
    public:
        BigSpace(size_t size=2ull*(1ull<<29)) : space_(nullptr), end_(nullptr), ptr_(nullptr), size_(size) {
            #ifdef CONFIG_BIGPIGO
            // Find an available file
            string dname;
            random_device rdev;
            mt19937 rng {rdev()};
            uniform_int_distribution<mt19937::result_type> rdist {0, 65535};
            while (true) {
                dname_ = PIGO_SCRATCH "bigspace." + to_string(rdist(rng));
                if (mkdir(dname_.c_str(), 0700) == 0)
                    break;
            }
            // Create a PIGO file with the desired size
            fname_ = dname_ + "/space";
            file_ = new pigo::WFile(fname_, size_);
            space_ = (char*)file_->fp();
            #else
            space_ = (char*)malloc(sizeof(char)*size_);
            if (space_ == nullptr)
                throw std::bad_alloc();
            space_ = (char*)memset(space_, 0xff, size_);
            #endif

            end_ = space_+size_;
            ptr_ = space_;
        }
        ~BigSpace() {
            #ifdef CONFIG_BIGPIGO
            delete file_;
            // Now, delete the actual file itself
            remove(fname_.c_str());
            rmdir(dname_.c_str());
            #else
            free(space_);
            #endif
        }

        void* allocate(size_t size) {
            // Anything larger than the whole space can never succeed (and
            // would overflow the size-class computation for huge values)
            if (size > size_) throw std::bad_alloc();
            size_t idx = size_class_index(size);
            size_t bytes = class_size(idx);

            std::lock_guard<std::mutex> lock {mtx_};
            void* head = free_heads_[idx];
            if (head != nullptr) {
                void* next;
                memcpy(&next, head, sizeof(void*));
                free_heads_[idx] = next;
                return head;
            }

            if (bytes > (size_t)(end_ - ptr_)) throw std::bad_alloc();
            char* res = ptr_;
            ptr_ += bytes;
            return (void*)res;
        }

        /** Return a block to its size-class free list. `size` must be the
         * size that was passed to allocate() for this block. */
        void deallocate(void* ptr, size_t size) {
            if (ptr == nullptr) return;
            size_t idx = size_class_index(size);

            std::lock_guard<std::mutex> lock {mtx_};
            assert((char*)ptr >= space_ && (char*)ptr < ptr_);
            void* head = free_heads_[idx];
            memcpy(ptr, &head, sizeof(void*));
            free_heads_[idx] = ptr;
        }

        BigSpace(const BigSpace& other) = delete;
        BigSpace(BigSpace&& other) noexcept = delete;
        BigSpace& operator=(const BigSpace& other) = delete;
        BigSpace& operator=(BigSpace&& other) noexcept = delete;
};

template <typename T>
class BigSpaceAllocator {
    private:
        std::shared_ptr<BigSpace> sp_;

        template <typename U>
        friend class BigSpaceAllocator;
        template <typename A, typename B>
        friend bool operator==(const BigSpaceAllocator<A>&, const BigSpaceAllocator<B>&) noexcept;
    public:
        using value_type = T;
        using is_always_equal = std::false_type;

        BigSpaceAllocator() = delete;
        BigSpaceAllocator(std::shared_ptr<BigSpace> sp) : sp_(sp) { }

        template <typename U>
        struct rebind {
            using other = BigSpaceAllocator<U>;
        };

        BigSpaceAllocator(const BigSpaceAllocator& other) = default;
        BigSpaceAllocator(BigSpaceAllocator&& other) noexcept = default;
        BigSpaceAllocator& operator=(const BigSpaceAllocator& other) = default;
        BigSpaceAllocator& operator=(BigSpaceAllocator&& other) noexcept = default;

        template <typename U>
        BigSpaceAllocator(const BigSpaceAllocator<U>& other) : sp_(other.sp_) { }

        template <typename U>
        BigSpaceAllocator& operator=(const BigSpaceAllocator<U>& other) {
            sp_ = other.sp_;
            return *this;
        }

        template <typename U>
        BigSpaceAllocator(BigSpaceAllocator<U>&& other) noexcept : sp_(std::move(other.sp_)) { }

        template <typename U>
        BigSpaceAllocator& operator=(BigSpaceAllocator<U>&& other) noexcept {
            sp_ = std::move(other.sp_);
            return *this;
        }

        T* allocate(size_t n) {
            return (T*)(sp_->allocate(n*sizeof(T)));
        }
        void deallocate(T* data, size_t n) {
            sp_->deallocate(data, n*sizeof(T));
        }
};

template <typename A, typename B>
bool operator==(const BigSpaceAllocator<A>& a, const BigSpaceAllocator<B>& b) noexcept {
    return a.sp_ == b.sp_;
}

template <typename A, typename B>
bool operator!=(const BigSpaceAllocator<A>& a, const BigSpaceAllocator<B>& b) noexcept {
    return !(a == b);
}
}
