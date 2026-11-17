#pragma once

/**
 * Processing statistics shared by the DBs (which collect them), ParDB (which
 * publishes them), and pando_top (which displays them).
 *
 * Kept free of ZeroMQ and DB dependencies so it can be used and tested on its
 * own.
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "pack.hpp"

namespace pando {

/** @brief The processing state of a ParDB agent */
typedef enum state_t {
    PRELOAD,
    STAGE_BEGIN,
    STAGE_PROCESSED,
    STAGE_CLOSING,
    STAGE_ALG_PROCESS
} state_t;

/** @brief Full state name, as reported by GET_STATE */
inline const char* state_name(uint8_t s) {
    switch (s) {
        case PRELOAD: return "PRELOAD";
        case STAGE_BEGIN: return "STAGE_BEGIN";
        case STAGE_PROCESSED: return "STAGE_PROCESSED";
        case STAGE_CLOSING: return "STAGE_CLOSING";
        case STAGE_ALG_PROCESS: return "STAGE_ALG_PROCESS";
    }
    return "UNKNOWN";
}

/** @brief Short state name for narrow displays */
inline const char* state_short_name(uint8_t s) {
    switch (s) {
        case PRELOAD: return "idle";
        case STAGE_BEGIN: return "filters";
        case STAGE_PROCESSED: return "processed";
        case STAGE_CLOSING: return "closing";
        case STAGE_ALG_PROCESS: return "group";
    }
    return "?";
}

/** @brief Which kind of filters a pass runs */
enum pass_kind_t : uint8_t {
    PASS_NONE = 0,      // no pass has started yet
    PASS_SINGLE = 1,    // SINGLE_ENTRY filters (one sweep over the entries)
    PASS_GROUP = 2      // GROUP_ENTRIES filters (the alg/graph process)
};

inline const char* pass_kind_name(uint8_t k) {
    switch (k) {
        case PASS_SINGLE: return "single";
        case PASS_GROUP: return "group";
    }
    return "-";
}

/** @brief Milliseconds since the Unix epoch, for cross-agent timestamps */
inline uint64_t wall_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/** @brief Counters for one filter over some span (a pass, or all passes) */
struct FilterCounters {
    uint64_t checked = 0;       // entries should_run() was asked about
    uint64_t ran = 0;           // entries the filter actually ran on
    uint64_t run_ns = 0;        // time spent inside run()
    uint64_t entries_out = 0;   // new entries the filter created
    uint64_t tags_added = 0;
    uint64_t tags_removed = 0;
    uint64_t val_updates = 0;

    FilterCounters& operator+=(const FilterCounters& o) {
        checked += o.checked;
        ran += o.ran;
        run_ns += o.run_ns;
        entries_out += o.entries_out;
        tags_added += o.tags_added;
        tags_removed += o.tags_removed;
        val_updates += o.val_updates;
        return *this;
    }
    FilterCounters operator+(const FilterCounters& o) const {
        FilterCounters r = *this;
        r += o;
        return r;
    }
    bool operator==(const FilterCounters& o) const {
        return checked == o.checked && ran == o.ran && run_ns == o.run_ns &&
            entries_out == o.entries_out && tags_added == o.tags_added &&
            tags_removed == o.tags_removed && val_updates == o.val_updates;
    }
    bool operator!=(const FilterCounters& o) const { return !(*this == o); }
};

/** @brief Per-filter stats: the current pass, plus everything before it */
struct FilterStats {
    uint8_t kind = PASS_SINGLE;
    FilterCounters pass;
    FilterCounters prior;

    FilterCounters total() const { return prior + pass; }
};

/** @brief A finished pass, as kept in ProcessStats::history */
struct PassSummary {
    uint64_t pass = 0;
    uint8_t kind = PASS_NONE;
    uint64_t start_ms = 0;
    uint64_t duration_ms = 0;
    uint64_t keys_total = 0;
    bool stopped_early = false;
    std::map<std::string, FilterCounters> filters;
};

/** @brief Processing statistics collected by one DB */
struct ProcessStats {
    /** Number of the current (or last) pass; 0 before the first pass */
    uint64_t pass = 0;
    uint8_t pass_kind = PASS_NONE;
    uint64_t pass_start_ms = 0;

    /** Progress of the current SINGLE_ENTRY sweep */
    uint64_t keys_total = 0;
    uint64_t keys_scanned = 0;
    /** The sweep stopped at MAX_FILTERS_TO_RUN and will continue next pass */
    bool stopped_early = false;

    /** Iteration of the current GROUP_ENTRIES computation */
    uint64_t group_iter = 0;

    /** Keyed by filter name; std::map keeps references stable and sorted */
    std::map<std::string, FilterStats> filters;

    /** Finished passes, oldest first, capped at max_history */
    std::deque<PassSummary> history;
    static constexpr size_t max_history = 256;

    /** @brief Get (creating if needed) the stats for a filter */
    FilterStats& filter(const std::string& name, uint8_t kind) {
        auto& f = filters[name];
        f.kind = kind;
        return f;
    }

    /** @brief Summarize the current pass */
    PassSummary current_summary() const {
        PassSummary s;
        s.pass = pass;
        s.kind = pass_kind;
        s.start_ms = pass_start_ms;
        uint64_t now = wall_ms();
        s.duration_ms = (pass_start_ms > 0 && now > pass_start_ms) ? now - pass_start_ms : 0;
        s.keys_total = keys_total;
        s.stopped_early = stopped_early;
        // Only the filters this kind of pass runs, so that e.g. a GROUP_ENTRIES
        // filter is not reported as having run on nothing in a SINGLE_ENTRY pass
        for (auto& [name, f] : filters)
            if (f.kind == pass_kind)
                s.filters[name] = f.pass;
        return s;
    }

    /** @brief Close the current pass (if any) and start a new one */
    void begin_pass(uint8_t kind) {
        end_pass();
        ++pass;
        pass_kind = kind;
        pass_start_ms = wall_ms();
        keys_total = 0;
        keys_scanned = 0;
        stopped_early = false;
        group_iter = 0;
    }

    /** @brief Record the current pass in the history and fold its counters
     * into the totals. Safe to call more than once. */
    void end_pass() {
        if (pass_kind == PASS_NONE) return;
        history.push_back(current_summary());
        while (history.size() > max_history) history.pop_front();
        for (auto& [name, f] : filters) {
            f.prior += f.pass;
            f.pass = FilterCounters{};
        }
        pass_kind = PASS_NONE;
    }
};

// ---------------------------------------------------------------------------
// Wire format
//
// Both messages are published by ParDB. All integers are host byte order, as
// with the rest of the Pando protocol.

/** @brief Bounds-checked reader for a message payload */
class StatsReader {
    private:
        const char* p_;
        const char* end_;
        bool ok_ = true;
    public:
        StatsReader(const char* data, const char* end) : p_(data), end_(end) { }

        template <typename T>
        T get() {
            T t{};
            if (!ok_ || end_ - p_ < (ptrdiff_t)sizeof(T)) { ok_ = false; return t; }
            std::memcpy(&t, p_, sizeof(T));
            p_ += sizeof(T);
            return t;
        }

        std::string get_string() {
            if (!ok_) return {};
            const void* nul = std::memchr(p_, '\0', end_ - p_);
            if (nul == nullptr) { ok_ = false; return {}; }
            std::string s { p_, (const char*)nul };
            p_ = (const char*)nul + 1;
            return s;
        }

        bool ok() const { return ok_; }
        bool done() const { return p_ == end_; }
};

/** @brief Appends values to a message payload */
class StatsWriter {
    private:
        std::string buf_;
    public:
        explicit StatsWriter(msg_type_t type) { put(type); }

        template <typename T>
        void put(T t) {
            buf_.append((const char*)&t, sizeof(T));
        }
        void put_string(const std::string& s) {
            buf_.append(s);
            buf_.push_back('\0');
        }

        const char* data() const { return buf_.data(); }
        size_t size() const { return buf_.size(); }
};

inline void write_counters(StatsWriter& w, const FilterCounters& c) {
    w.put(c.checked);
    w.put(c.ran);
    w.put(c.run_ns);
    w.put(c.entries_out);
    w.put(c.tags_added);
    w.put(c.tags_removed);
    w.put(c.val_updates);
}

inline FilterCounters read_counters(StatsReader& r) {
    FilterCounters c;
    c.checked = r.get<uint64_t>();
    c.ran = r.get<uint64_t>();
    c.run_ns = r.get<uint64_t>();
    c.entries_out = r.get<uint64_t>();
    c.tags_added = r.get<uint64_t>();
    c.tags_removed = r.get<uint64_t>();
    c.val_updates = r.get<uint64_t>();
    return c;
}

/** @brief One filter's counters as carried in a PROGRESS message */
struct FilterSnapshot {
    uint8_t kind = PASS_SINGLE;
    FilterCounters pass;
    FilterCounters total;
};

/** @brief Payload of a PROGRESS message: an agent's processing state */
struct ProgressSnapshot {
    addr_t agent = 0;
    uint64_t time_ms = 0;
    uint64_t db_size = 0;
    uint8_t state = PRELOAD;
    bool at_barrier = false;
    uint64_t pass = 0;
    uint8_t pass_kind = PASS_NONE;
    uint64_t pass_start_ms = 0;
    uint64_t keys_total = 0;
    uint64_t keys_scanned = 0;
    bool stopped_early = false;
    uint64_t group_iter = 0;
    std::map<std::string, FilterSnapshot> filters;

    /** @brief Fill the processing fields from a DB's stats */
    void set_stats(const ProcessStats& s) {
        pass = s.pass;
        pass_kind = s.pass_kind;
        pass_start_ms = s.pass_start_ms;
        keys_total = s.keys_total;
        keys_scanned = s.keys_scanned;
        stopped_early = s.stopped_early;
        group_iter = s.group_iter;
        filters.clear();
        for (auto& [name, f] : s.filters)
            filters[name] = FilterSnapshot { f.kind, f.pass, f.total() };
    }

    void serialize(StatsWriter& w) const {
        w.put(agent);
        w.put(time_ms);
        w.put(db_size);
        w.put(state);
        w.put((uint8_t)at_barrier);
        w.put(pass);
        w.put(pass_kind);
        w.put(pass_start_ms);
        w.put(keys_total);
        w.put(keys_scanned);
        w.put((uint8_t)stopped_early);
        w.put(group_iter);
        w.put((uint64_t)filters.size());
        for (auto& [name, f] : filters) {
            w.put_string(name);
            w.put(f.kind);
            write_counters(w, f.pass);
            write_counters(w, f.total);
        }
    }

    /** @brief Parse a payload (after the message type); false if malformed */
    bool deserialize(const char* data, const char* end) {
        StatsReader r { data, end };
        agent = r.get<addr_t>();
        time_ms = r.get<uint64_t>();
        db_size = r.get<uint64_t>();
        state = r.get<uint8_t>();
        at_barrier = r.get<uint8_t>() != 0;
        pass = r.get<uint64_t>();
        pass_kind = r.get<uint8_t>();
        pass_start_ms = r.get<uint64_t>();
        keys_total = r.get<uint64_t>();
        keys_scanned = r.get<uint64_t>();
        stopped_early = r.get<uint8_t>() != 0;
        group_iter = r.get<uint64_t>();
        uint64_t n = r.get<uint64_t>();
        filters.clear();
        for (uint64_t i = 0; i < n && r.ok(); ++i) {
            std::string name = r.get_string();
            FilterSnapshot f;
            f.kind = r.get<uint8_t>();
            f.pass = read_counters(r);
            f.total = read_counters(r);
            filters[name] = f;
        }
        return r.ok() && r.done();
    }
};

/** @brief What a STAGE_EVENT reports */
enum stage_event_kind_t : uint8_t {
    EVENT_STATE_CHANGE = 0,   // the agent moved from one state to another
    EVENT_PASS_END = 1        // the agent finished a pass; carries a summary
};

/** @brief Payload of a STAGE_EVENT message */
struct StageEvent {
    addr_t agent = 0;
    uint64_t time_ms = 0;
    uint8_t kind = EVENT_STATE_CHANGE;
    uint8_t from_state = PRELOAD;
    uint8_t to_state = PRELOAD;
    /** For EVENT_PASS_END: the finished pass. Filters are only set then. */
    PassSummary summary;

    void serialize(StatsWriter& w) const {
        w.put(agent);
        w.put(time_ms);
        w.put(kind);
        w.put(from_state);
        w.put(to_state);
        w.put(summary.pass);
        w.put(summary.kind);
        w.put(summary.start_ms);
        w.put(summary.duration_ms);
        w.put(summary.keys_total);
        w.put((uint8_t)summary.stopped_early);
        w.put((uint64_t)summary.filters.size());
        for (auto& [name, c] : summary.filters) {
            w.put_string(name);
            write_counters(w, c);
        }
    }

    bool deserialize(const char* data, const char* end) {
        StatsReader r { data, end };
        agent = r.get<addr_t>();
        time_ms = r.get<uint64_t>();
        kind = r.get<uint8_t>();
        from_state = r.get<uint8_t>();
        to_state = r.get<uint8_t>();
        summary = PassSummary{};
        summary.pass = r.get<uint64_t>();
        summary.kind = r.get<uint8_t>();
        summary.start_ms = r.get<uint64_t>();
        summary.duration_ms = r.get<uint64_t>();
        summary.keys_total = r.get<uint64_t>();
        summary.stopped_early = r.get<uint8_t>() != 0;
        uint64_t n = r.get<uint64_t>();
        for (uint64_t i = 0; i < n && r.ok(); ++i) {
            std::string name = r.get_string();
            summary.filters[name] = read_counters(r);
        }
        return r.ok() && r.done();
    }
};

}
