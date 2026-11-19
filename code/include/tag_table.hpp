#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pando {

/** @brief One table, per process, of every distinct tag string ("interning")
 *
 * Each distinct tag is stored once and given a small integer id; entries
 * (TagStore) keep the ids instead of their own copies of the strings. Tags in
 * a database usually repeat heavily (every k-mer count entry has the same few
 * tags), so this saves most of the memory tags used to take.
 *
 * The table only grows: a tag stays in it for the life of the process, so
 * the strings it hands out stay valid. That suits tags that repeat; a
 * workload that gives entries unique tags would make the table grow with
 * every entry (a warning is printed once the table gets large), and should
 * turn interning off (set_interning(false), pando_pardb --intern-tags=off).
 *
 * Safe to use from several threads (an agent's ParDB, map and responder
 * threads all use it).
 */
class TagTable {
    public:
        /** @brief The process's table */
        static TagTable& global() {
            static TagTable table;
            return table;
        }

        /** @brief Choose whether entries created from now on intern their tags
         * (default on). Entries keep the choice they were created with. */
        static void set_interning(bool on) { interning_.store(on); }

        /** @brief Whether entries created now intern their tags */
        static bool interning() { return interning_.load(); }

        /** @brief Return the id for a tag, adding it to the table if new */
        uint32_t intern(std::string_view tag) {
            {
                std::shared_lock<std::shared_mutex> lock {mtx_};
                auto it = ids_.find(tag);
                if (it != ids_.end()) return it->second;
            }
            std::unique_lock<std::shared_mutex> lock {mtx_};
            auto it = ids_.find(tag);
            if (it != ids_.end()) return it->second;
            if (by_id_.size() >= UINT32_MAX) throw std::length_error("TagTable: too many distinct tags");
            uint32_t id = (uint32_t)by_id_.size();
            strings_.emplace_back(tag);
            const std::string& stored = strings_.back();
            by_id_.push_back(&stored);
            ids_.emplace(std::string_view {stored}, id);
            if (by_id_.size() == WARN_SIZE)
                std::cerr << "[Pando] [WARNING] the tag table holds " << WARN_SIZE << " distinct tags. Interning "
                             "assumes tags repeat; if entries get unique tags, use --intern-tags=off" << std::endl;
            return id;
        }

        /** @brief Find the id of a tag without adding it
         * @return false if the tag has never been interned */
        bool find(std::string_view tag, uint32_t& id) const {
            std::shared_lock<std::shared_mutex> lock {mtx_};
            auto it = ids_.find(tag);
            if (it == ids_.end()) return false;
            id = it->second;
            return true;
        }

        /** @brief The tag with this id (valid for the life of the process) */
        const std::string& str(uint32_t id) const {
            std::shared_lock<std::shared_mutex> lock {mtx_};
            if (id >= by_id_.size()) throw std::out_of_range("TagTable: unknown tag id");
            return *by_id_[id];
        }

        /** @brief Number of distinct tags */
        size_t size() const {
            std::shared_lock<std::shared_mutex> lock {mtx_};
            return by_id_.size();
        }

        static constexpr size_t WARN_SIZE = 1u << 20;

    private:
        TagTable() = default;

        mutable std::shared_mutex mtx_;
        /** Every tag; a deque keeps elements in place as it grows */
        std::deque<std::string> strings_;
        /** id -> tag */
        std::vector<const std::string*> by_id_;
        /** tag -> id (views into strings_) */
        std::unordered_map<std::string_view, uint32_t> ids_;

        static inline std::atomic<bool> interning_ {true};
};

/** @brief The set of tags of one entry
 *
 * Interned (the default): a sorted array of TagTable ids, kept inside the
 * object for up to INLINE tags and on the heap beyond that, so most entries
 * need no allocation for their tags.
 *
 * Not interned: each entry owns its strings (an unordered_set, allocated on
 * the first tag), as entries did before interning.
 *
 * The mode is chosen when the store is created (TagTable::interning()) and
 * kept by copies and moves. Stores of different modes compare and copy
 * correctly, through their strings.
 */
class TagStore {
    public:
        static constexpr uint16_t INLINE = 4;

        TagStore() : size_(0), cap_(INLINE), interned_(TagTable::interning()) {
            reset_storage_();
        }
        ~TagStore() { release_(); }

        TagStore(const TagStore& other) : size_(0), cap_(INLINE), interned_(other.interned_) {
            reset_storage_();
            copy_from_(other);
        }
        TagStore& operator=(const TagStore& other) {
            if (&other != this) {
                release_();
                size_ = 0;
                cap_ = INLINE;
                interned_ = other.interned_;
                reset_storage_();
                copy_from_(other);
            }
            return *this;
        }
        TagStore(TagStore&& other) noexcept : size_(0), cap_(INLINE), interned_(other.interned_) {
            reset_storage_();
            steal_(other);
        }
        TagStore& operator=(TagStore&& other) noexcept {
            if (&other != this) {
                release_();
                interned_ = other.interned_;
                steal_(other);
            }
            return *this;
        }

        /** @brief Whether this store keeps TagTable ids (true) or strings */
        bool interned() const { return interned_; }

        size_t size() const {
            if (interned_) return size_;
            return strs_ ? strs_->size() : 0;
        }

        /** @brief Add a tag (no effect if present) */
        void insert(std::string_view tag) {
            if (!interned_) {
                if (!strs_) strs_ = new std::unordered_set<std::string>;
                strs_->emplace(tag);
                return;
            }
            uint32_t id = TagTable::global().intern(tag);
            uint32_t* ids = ids_();
            uint32_t* pos = lower_bound_(ids, size_, id);
            if (pos != ids + size_ && *pos == id) return;
            size_t at = pos - ids;
            if (size_ == cap_) {
                grow_();
                ids = ids_();
            }
            memmove(ids + at + 1, ids + at, (size_ - at) * sizeof(uint32_t));
            ids[at] = id;
            ++size_;
        }

        /** @brief Remove a tag (no effect if absent) */
        void erase(std::string_view tag) {
            if (!interned_) {
                if (strs_) strs_->erase(std::string {tag});
                return;
            }
            uint32_t id;
            if (!TagTable::global().find(tag, id)) return;
            uint32_t* ids = ids_();
            uint32_t* pos = lower_bound_(ids, size_, id);
            if (pos == ids + size_ || *pos != id) return;
            size_t at = pos - ids;
            memmove(ids + at, ids + at + 1, (size_ - at - 1) * sizeof(uint32_t));
            --size_;
        }

        /** @brief Whether a tag is present */
        bool contains(std::string_view tag) const {
            if (!interned_) return strs_ && strs_->count(std::string {tag}) > 0;
            uint32_t id;
            if (!TagTable::global().find(tag, id)) return false;
            const uint32_t* ids = ids_();
            const uint32_t* pos = lower_bound_(ids, size_, id);
            return pos != ids + size_ && *pos == id;
        }

        /** @brief Remove every tag */
        void clear() {
            release_();
            size_ = 0;
            cap_ = INLINE;
            reset_storage_();
        }

        /** @brief Call f(const std::string&) for each tag. Strings are valid
         * until the store changes (interned ones, for the life of the process). */
        template<typename F>
        void for_each(F&& f) const {
            if (interned_) {
                const uint32_t* ids = ids_();
                TagTable& table = TagTable::global();
                for (uint16_t i = 0; i < size_; ++i)
                    f(table.str(ids[i]));
            } else if (strs_) {
                for (const std::string& tag : *strs_)
                    f(tag);
            }
        }

        /** @brief The tags as a set of strings */
        std::unordered_set<std::string> to_set() const {
            std::unordered_set<std::string> res;
            res.reserve(size());
            for_each([&](const std::string& tag) { res.insert(tag); });
            return res;
        }

        /** @brief Whether both hold the same tags */
        bool equals(const TagStore& other) const {
            if (size() != other.size()) return false;
            if (interned_ && other.interned_)
                return memcmp(ids_(), other.ids_(), size_ * sizeof(uint32_t)) == 0;
            bool same = true;
            for_each([&](const std::string& tag) { if (same && !other.contains(tag)) same = false; });
            return same;
        }

    private:
        uint16_t size_;     // interned: number of ids
        uint16_t cap_;      // interned: capacity (INLINE = the inline array)
        bool interned_;
        union {
            uint32_t inline_[INLINE];
            uint32_t* heap_;
            std::unordered_set<std::string>* strs_;
        };

        /** Empty storage for the current mode (does not free anything) */
        void reset_storage_() {
            if (interned_) memset(inline_, 0, sizeof(inline_));
            else strs_ = nullptr;
        }

        uint32_t* ids_() { return cap_ == INLINE ? inline_ : heap_; }
        const uint32_t* ids_() const { return cap_ == INLINE ? inline_ : heap_; }

        static uint32_t* lower_bound_(uint32_t* ids, size_t n, uint32_t id) {
            size_t lo = 0, hi = n;
            while (lo < hi) {
                size_t mid = (lo + hi) / 2;
                if (ids[mid] < id) lo = mid + 1; else hi = mid;
            }
            return ids + lo;
        }
        static const uint32_t* lower_bound_(const uint32_t* ids, size_t n, uint32_t id) {
            return lower_bound_(const_cast<uint32_t*>(ids), n, id);
        }

        void grow_() {
            if (cap_ == UINT16_MAX) throw std::length_error("TagStore: too many tags on one entry");
            size_t new_cap = (size_t)cap_ * 2;
            if (new_cap > UINT16_MAX) new_cap = UINT16_MAX;
            uint32_t* bigger = new uint32_t[new_cap];
            memcpy(bigger, ids_(), size_ * sizeof(uint32_t));
            if (cap_ != INLINE) delete [] heap_;
            heap_ = bigger;
            cap_ = (uint16_t)new_cap;
        }

        void release_() {
            if (interned_) {
                if (cap_ != INLINE) delete [] heap_;
            } else {
                delete strs_;
            }
        }

        void copy_from_(const TagStore& other) {
            if (interned_ && other.interned_) {
                if (other.size_ > INLINE) {
                    heap_ = new uint32_t[other.cap_];
                    cap_ = other.cap_;
                }
                memcpy(ids_(), other.ids_(), other.size_ * sizeof(uint32_t));
                size_ = other.size_;
            } else {
                other.for_each([&](const std::string& tag) { insert(tag); });
            }
        }

        /** Take other's tags; other is left empty, in its mode */
        void steal_(TagStore& other) noexcept {
            if (interned_) {
                size_ = other.size_;
                cap_ = other.cap_;
                if (cap_ == INLINE) memcpy(inline_, other.inline_, sizeof(inline_));
                else heap_ = other.heap_;
                other.size_ = 0;
                other.cap_ = INLINE;
                memset(other.inline_, 0, sizeof(other.inline_));
            } else {
                strs_ = other.strs_;
                other.strs_ = nullptr;
            }
        }
};

}
