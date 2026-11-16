#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <exception>
#include <locale.h>
#include <map>
#include <ncurses.h>
#include <set>
#include <string>
#include <sys/ioctl.h>
#include <vector>

#include "address.hpp"
#include "par_db.hpp"
#include "process_stats.hpp"

#include "util.hpp"


using namespace std;
using namespace pando;
using steady = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Formatting helpers

/** @brief Exact if it fits in width, else compact (12.3k, 4.56M, ...) */
static string fmt_count(uint64_t n, size_t width = 0) {
    string exact = to_string(n);
    if (width == 0 ? n < 10000 : exact.size() <= width) return exact;
    const char* units = "kMGTP";
    double v = (double)n;
    int u = -1;
    while (v >= 1000.0 && u < 4) { v /= 1000.0; ++u; }
    char buf[32];
    snprintf(buf, sizeof(buf), v < 10.0 ? "%.2f%c" : v < 100.0 ? "%.1f%c" : "%.0f%c", v, units[u]);
    return buf;
}

static string fmt_rate(double per_s) {
    if (per_s <= 0.0) return "-";
    if (per_s < 10.0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", per_s);
        return buf;
    }
    return fmt_count((uint64_t)llround(per_s));
}

static string fmt_duration_ms(uint64_t ms) {
    char buf[32];
    if (ms < 1000) snprintf(buf, sizeof(buf), "%llums", (unsigned long long)ms);
    else if (ms < 60000) snprintf(buf, sizeof(buf), "%.1fs", ms / 1000.0);
    else if (ms < 3600000) snprintf(buf, sizeof(buf), "%llum%02llus", (unsigned long long)(ms / 60000), (unsigned long long)(ms / 1000 % 60));
    else snprintf(buf, sizeof(buf), "%lluh%02llum", (unsigned long long)(ms / 3600000), (unsigned long long)(ms / 60000 % 60));
    return buf;
}

static string fmt_clock_ms(uint64_t ms) {
    time_t secs = ms / 1000;
    struct tm tm;
    localtime_r(&secs, &tm);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(ms % 1000));
    return buf;
}

static string pad(const string& s, size_t width, bool right = false) {
    if (s.size() >= width) return s.substr(0, width);
    string fill(width - s.size(), ' ');
    return right ? fill + s : s + fill;
}

/** @brief Print s at (y, x), truncated to width and padded to clear old text */
static void put(WINDOW* w, int y, int x, int width, const string& s) {
    if (width <= 0) return;
    string p = pad(s, width);
    mvwaddnstr(w, y, x, p.c_str(), width);
}

static string addr_str(addr_t a) {
    static map<addr_t, string> cache;
    auto it = cache.find(a);
    if (it != cache.end()) return it->second;
    elga::ZMQAddress elga_addr {a};
    return cache[a] = elga_addr.get_addr_str();
}

// ---------------------------------------------------------------------------
// The data pando_top displays

struct AgentInfo {
    time_t last_seen = 0;       // last STATS or PROGRESS message
    size_t db_size = 0;
    bool has_progress = false;  // false for agents that only send STATS
    ProgressSnapshot progress;

    /** Recent progress, for rates */
    struct Sample {
        steady::time_point t;
        uint64_t pass;
        uint64_t keys_scanned;
        map<string, uint64_t> ran;
    };
    deque<Sample> samples;
};

/** @brief A pass, summed over the agents that have finished it */
struct PassRecord {
    uint8_t kind = PASS_NONE;
    map<string, FilterCounters> filters;
    set<addr_t> finished;
    uint64_t max_duration_ms = 0;
};

static const auto RATE_WINDOW = chrono::seconds(10);
static const size_t MAX_EVENTS = 2000;
static const size_t MAX_PASSES = 256;

class PandoTop : public PandoParticipant {
    private:
        map<addr_t, AgentInfo> info_;
        map<uint64_t, PassRecord> passes_;
        deque<StageEvent> events_;
        size_t bad_msgs_ = 0;
        bool changed_ = true;

        void recv_progress(const char* data, const char* end) {
            ProgressSnapshot p;
            if (!p.deserialize(data, end)) { ++bad_msgs_; return; }
            auto& ai = info_[p.agent];
            ai.last_seen = time(nullptr);
            ai.db_size = p.db_size;
            ai.has_progress = true;

            AgentInfo::Sample s { steady::now(), p.pass, p.keys_scanned, {} };
            for (auto& [name, f] : p.filters) s.ran[name] = f.total.ran;
            ai.samples.push_back(move(s));
            while (ai.samples.size() > 2 && ai.samples.front().t < steady::now() - RATE_WINDOW)
                ai.samples.pop_front();

            ai.progress = move(p);
        }

        void recv_stage_event(const char* data, const char* end) {
            StageEvent e;
            if (!e.deserialize(data, end)) { ++bad_msgs_; return; }
            if (e.kind == EVENT_PASS_END) {
                auto& rec = passes_[e.summary.pass];
                rec.kind = e.summary.kind;
                if (rec.finished.insert(e.agent).second) {
                    for (auto& [name, c] : e.summary.filters) rec.filters[name] += c;
                    rec.max_duration_ms = max(rec.max_duration_ms, e.summary.duration_ms);
                }
                while (passes_.size() > MAX_PASSES) passes_.erase(passes_.begin());
            }
            events_.push_back(move(e));
            while (events_.size() > MAX_EVENTS) events_.pop_front();
        }

    public:
        PandoTop() : PandoParticipant(elga::ZMQAddress{}, false) {
            sub(STATS);
            sub(PROGRESS);
            sub(STAGE_EVENT);
        }
        void process_msg([[maybe_unused]] msg_type_t type, [[maybe_unused]] zmq_socket_t sock, [[maybe_unused]] const char *data, [[maybe_unused]] const char *end) {
            changed_ = true;
            if (type == STATS) {
                addr_t agent;
                size_t size;
                unpack_single(data, agent);
                unpack_single(data, size);
                auto& ai = info_[agent];
                ai.last_seen = time(nullptr);
                ai.db_size = size;
            } else if (type == PROGRESS) {
                recv_progress(data, end);
            } else if (type == STAGE_EVENT) {
                recv_stage_event(data, end);
            } else
                recv_unknown_msg();
        }

        /** @brief Returns whether anything arrived since the last call */
        bool take_changed() { bool c = changed_; changed_ = false; return c; }

        const AgentInfo* info(addr_t a) const {
            auto it = info_.find(a);
            return it == info_.end() ? nullptr : &it->second;
        }
        const map<addr_t, AgentInfo>& infos() const { return info_; }
        const map<uint64_t, PassRecord>& passes() const { return passes_; }
        const deque<StageEvent>& events() const { return events_; }
        size_t bad_msgs() const { return bad_msgs_; }

        /** @brief The latest pass any agent has started */
        uint64_t mesh_pass() const {
            uint64_t p = 0;
            for (auto& [a, ai] : info_)
                if (ai.has_progress) p = max(p, ai.progress.pass);
            return p;
        }

        size_t agents_with_progress() const {
            size_t n = 0;
            for (auto& [a, ai] : info_) n += ai.has_progress;
            return n;
        }

        /** @brief Whether some agent is still working on the pass */
        bool pass_live(uint64_t pass) const {
            auto it = passes_.find(pass);
            size_t finished = it == passes_.end() ? 0 : it->second.finished.size();
            return pass > 0 && finished < agents_with_progress();
        }

        /** @brief Counters for a pass: finished agents' summaries plus the
         * live counters of agents still in it */
        map<string, FilterCounters> pass_counters(uint64_t pass) const {
            map<string, FilterCounters> res;
            const set<addr_t>* finished = nullptr;
            auto it = passes_.find(pass);
            if (it != passes_.end()) {
                res = it->second.filters;
                finished = &it->second.finished;
            }
            for (auto& [a, ai] : info_) {
                if (!ai.has_progress) continue;
                auto& p = ai.progress;
                if (p.pass != pass || p.pass_kind == PASS_NONE) continue;
                if (finished != nullptr && finished->count(a)) continue;
                for (auto& [name, f] : p.filters) res[name] += f.pass;
            }
            return res;
        }

        map<string, FilterCounters> total_counters() const {
            map<string, FilterCounters> res;
            for (auto& [a, ai] : info_)
                if (ai.has_progress)
                    for (auto& [name, f] : ai.progress.filters) res[name] += f.total;
            return res;
        }

        map<string, uint8_t> filter_kinds() const {
            map<string, uint8_t> res;
            for (auto& [a, ai] : info_)
                if (ai.has_progress)
                    for (auto& [name, f] : ai.progress.filters) res[name] = f.kind;
            return res;
        }

        /** @brief Mesh-wide runs per second of each filter, over the recent window */
        map<string, double> run_rates() const {
            map<string, double> res;
            for (auto& [a, ai] : info_) {
                if (ai.samples.size() < 2) continue;
                auto& first = ai.samples.front();
                auto& last = ai.samples.back();
                double dt = chrono::duration<double>(last.t - first.t).count();
                if (dt <= 0.0) continue;
                for (auto& [name, ran] : last.ran) {
                    auto it = first.ran.find(name);
                    uint64_t before = it == first.ran.end() ? 0 : it->second;
                    if (ran > before) res[name] += (ran - before) / dt;
                }
            }
            return res;
        }

        /** @brief Entries/s an agent is scanning in its current pass (0 if unknown) */
        static double scan_rate(const AgentInfo& ai) {
            if (ai.samples.size() < 2) return 0.0;
            auto& last = ai.samples.back();
            for (auto& s : ai.samples) {
                if (s.pass != last.pass) continue;
                double dt = chrono::duration<double>(last.t - s.t).count();
                if (dt <= 0.0 || last.keys_scanned <= s.keys_scanned) return 0.0;
                return (last.keys_scanned - s.keys_scanned) / dt;
            }
            return 0.0;
        }
};

/** @brief Whether an agent is sweeping its entries right now */
static bool scanning(const AgentInfo& ai) {
    auto& p = ai.progress;
    return ai.has_progress && p.state == STAGE_BEGIN && !p.at_barrier &&
        p.pass_kind == PASS_SINGLE && p.keys_total > 0;
}

/** @brief Agents holding up a barrier: still working while others wait */
static vector<addr_t> stragglers(const PandoTop& pp, size_t* waiting = nullptr, size_t* total = nullptr) {
    vector<addr_t> working;
    size_t w = 0, n = 0;
    for (auto& [a, ai] : pp.infos()) {
        if (!ai.has_progress) continue;
        ++n;
        if (ai.progress.at_barrier) ++w;
        else if (ai.progress.state != PRELOAD) working.push_back(a);
    }
    if (waiting) *waiting = w;
    if (total) *total = n;
    if (w == 0) working.clear();
    return working;
}

/** @brief One-line summary of where the mesh is in processing */
static string progress_line(const PandoTop& pp) {
    if (pp.agents_with_progress() == 0) return "no progress reports yet";

    map<uint8_t, size_t> states;
    uint64_t scanned = 0, total = 0;
    double eta_s = 0.0;
    bool eta_known = true, any_scanning = false;
    uint64_t mesh_pass = pp.mesh_pass();
    uint8_t kind = PASS_NONE;
    for (auto& [a, ai] : pp.infos()) {
        if (!ai.has_progress) continue;
        auto& p = ai.progress;
        ++states[p.state];
        if (p.pass == mesh_pass && p.pass_kind != PASS_NONE) kind = p.pass_kind;
        if (p.pass == mesh_pass && p.pass_kind == PASS_SINGLE) {
            scanned += p.keys_scanned;
            total += p.keys_total;
        }
        if (scanning(ai)) {
            any_scanning = true;
            double rate = PandoTop::scan_rate(ai);
            if (rate > 0.0) eta_s = max(eta_s, (p.keys_total - p.keys_scanned) / rate);
            else eta_known = false;
        }
    }

    string s;
    if (states.size() == 1 && states.count(PRELOAD)) {
        s = "idle";
        if (mesh_pass > 0) s += ", last pass " + to_string(mesh_pass);
        return s;
    }

    s = "pass " + to_string(mesh_pass);
    if (kind != PASS_NONE) s += string(" (") + pass_kind_name(kind) + ")";
    if (states.size() == 1) s += string(" ") + state_short_name(states.begin()->first);
    else {
        // Most common state, then the rest
        auto top = max_element(states.begin(), states.end(), [](auto& x, auto& y) { return x.second < y.second; });
        s += string(" ") + state_short_name(top->first) + " x" + to_string(top->second) + " +" +
            to_string(pp.agents_with_progress() - top->second);
    }
    if (any_scanning && total > 0) {
        s += "  " + fmt_count(scanned) + "/" + fmt_count(total) + " " + to_string(100 * scanned / total) + "%";
        if (eta_known && eta_s > 0.0) s += " ETA " + fmt_duration_ms((uint64_t)(eta_s * 1000));
    }
    size_t waiting = 0, n = 0;
    stragglers(pp, &waiting, &n);
    if (waiting > 0 && waiting < n) s += "  barrier " + to_string(waiting) + "/" + to_string(n);
    return s;
}

// ---------------------------------------------------------------------------
// Terminal handling

volatile sig_atomic_t *g_shutdown = nullptr;

void si_handle(int) {
    cerr << "[Pando] Ctrl+C caught, shutting down..." << endl;
    if (g_shutdown == nullptr) throw runtime_error("Unable to find shutdown flag");
    *g_shutdown = true;
}
void set_handler() {
    struct sigaction si_handler;
    si_handler.sa_handler = si_handle;
    sigemptyset(&si_handler.sa_mask);
    si_handler.sa_flags = SA_RESTART;
    sigaction(SIGINT, &si_handler, NULL);
}

enum Screen { SCREEN_AGENTS = 0, SCREEN_FILTERS, SCREEN_PASSES, SCREEN_EVENTS, NUM_SCREENS };
static const char* screen_names[NUM_SCREENS] = { "Agents", "Filters", "Passes", "Events" };

/** @brief What the user has chosen to look at; survives layout rebuilds */
struct UiState {
    Screen screen = SCREEN_AGENTS;
    int scroll = 0;         // rows (from the top, or from the newest event)
    int hscroll = 0;        // passes to the left of the newest (passes screen)
    bool totals = false;    // filters screen: totals instead of current pass
    bool outputs = false;   // passes screen: new entries instead of runs
    void reset_scroll() { scroll = 0; hscroll = 0; }
};

// Color pairs
static const int C_RED = 1, C_BLUE = 2, C_MAGENTA = 3, C_YELLOW = 4;

// Layout: header boxes on rows 1-5, the tab bar on row 6, a rule on row 7,
// the screen body from row 8, and the last row for hints and notes.
static const int BODY_Y = 8;

// Smallest terminal the layout fits in: the stats box needs 31 columns
// ("Total DB size : " plus an 11-digit size) after the 14-column header and
// the 7-column spinner. Agent boxes are 4 rows tall, starting at BODY_Y, and
// the last row is kept for the footer, so one row of agents needs 13.
static const int MIN_ROWS = BODY_Y + 4 + 1;
static const int MIN_COLS = 52;

struct AgentBox {
    WINDOW* w = nullptr;
};

/** @brief The agent boxes (the original pando_top view) */
class AgentsView {
    private:
        PandoTop& pp_;
        map<addr_t, AgentBox> boxes_;
        size_t hidden_agents_ = 0;

        const int blk_h_ = 4;
        const int blk_w_ = 22;
        const int blk_pad_ = 1;

        int x_ = 2, y_ = BODY_Y;
        int rows_, cols_;

        void draw_box_(WINDOW* w, int color) {
            wattron(w, COLOR_PAIR(color));
            box(w, 0, 0);
            wattroff(w, COLOR_PAIR(color));
        }

    public:
        AgentsView(PandoTop& pp, int rows, int cols) : pp_(pp), rows_(rows), cols_(cols) { }
        ~AgentsView() {
            for (auto& [addr, b] : boxes_)
                if (b.w != nullptr) delwin(b.w);
        }
        AgentsView(const AgentsView&) = delete;
        AgentsView& operator=(const AgentsView&) = delete;

        size_t hidden() const { return hidden_agents_; }

        void draw() {
            auto late = stragglers(pp_);
            set<addr_t> late_set { late.begin(), late.end() };

            for (addr_t addr : pp_.agents()) {
                if (boxes_.count(addr) == 0) {
                    // A new agent: create a window for it, if one fits
                    AgentBox b;
                    if (x_ + blk_w_ + blk_pad_ >= cols_) {
                        // Move to the next line
                        y_ += blk_h_;
                        x_ = 2;
                    }
                    if (y_ + blk_h_ <= rows_ - 1 && x_ + blk_w_ <= cols_)
                        b.w = newwin(blk_h_, blk_w_, y_, x_);
                    if (b.w != nullptr) x_ += blk_w_ + blk_pad_;
                    else ++hidden_agents_;
                    boxes_[addr] = b;
                }

                WINDOW* w = boxes_[addr].w;
                if (w == nullptr) continue;

                const AgentInfo* ai = pp_.info(addr);
                time_t last_seen = ai ? ai->last_seen : 0;
                auto age = time(nullptr) - last_seen;
                if (age > 999) age = 999;

                // Stale beats straggling beats fresh
                if (age > 60) draw_box_(w, C_RED);
                else if (late_set.count(addr)) draw_box_(w, C_YELLOW);
                else if (age < 10) draw_box_(w, C_BLUE);
                else draw_box_(w, C_MAGENTA);

                if (age > 4) {
                    string age_s = to_string(age);
                    if (age == 999) age_s += "+";
                    mvwaddstr(w, 0, blk_w_-age_s.size()-1, age_s.c_str());
                }
                mvwaddnstr(w, 0, 1, addr_str(addr).c_str(), blk_w_-2);
                put(w, 1, 2, blk_w_-3, "DB size : " + to_string(ai ? ai->db_size : 0));

                string line;
                if (ai && ai->has_progress) {
                    auto& p = ai->progress;
                    if (p.pass > 0) line = "p" + to_string(p.pass) + " ";
                    line += state_short_name(p.state);
                    if (p.at_barrier) line += " @barrier";
                    else if (scanning(*ai)) line += " " + to_string(100 * p.keys_scanned / p.keys_total) + "%";
                    else if (p.state == STAGE_ALG_PROCESS && p.group_iter > 0) line += " it " + to_string(p.group_iter);
                }
                put(w, 2, 2, blk_w_-3, line);
                wnoutrefresh(w);
            }
        }
};

/** @brief A simple table column */
struct Col {
    string header;
    int width;
    bool right;
};

/** @brief Print a row of cells, dropping columns that do not fit */
static void put_row(WINDOW* w, int y, int width, const vector<Col>& cols, const vector<string>& cells, attr_t attr = A_NORMAL) {
    string line;
    for (size_t i = 0; i < cols.size() && i < cells.size(); ++i) {
        string cell = pad(cells[i], cols[i].width, cols[i].right);
        if ((int)(line.size() + cell.size()) > width) break;
        line += cell;
        if (i + 1 < cols.size()) line += " ";
    }
    if (attr != A_NORMAL) wattron(w, attr);
    put(w, y, 0, width, line);
    if (attr != A_NORMAL) wattroff(w, attr);
}

static int filter_name_width(const map<string, uint8_t>& kinds, int cols) {
    int n = 6;
    for (auto& [name, k] : kinds) n = max(n, (int)name.size());
    return min(n, max(12, cols / 3));
}

/** @brief Per-filter counts for the current pass or all passes */
static void draw_filters(WINDOW* w, PandoTop& pp, UiState& ui, int height, int width) {
    uint64_t pass = pp.mesh_pass();
    auto counters = ui.totals ? pp.total_counters() : pp.pass_counters(pass);
    auto kinds = pp.filter_kinds();
    for (auto& [name, c] : counters) kinds.emplace(name, PASS_SINGLE);
    auto rates = pp.run_rates();

    int y = 0;
    string title;
    if (ui.totals) title = "All passes (since each agent started)";
    else if (pass == 0) title = "No pass has run yet";
    else title = "Pass " + to_string(pass) + (pp.pass_live(pass) ? " (in progress)" : " (finished)");
    wattron(w, A_BOLD);
    put(w, y++, 0, width, title);
    wattroff(w, A_BOLD);
    y++;

    vector<Col> cols {
        {"FILTER", filter_name_width(kinds, width), false},
        {"TYPE", 6, false},
        {"CHECKED", 9, true},
        {"RAN", 9, true},
        {"NEW ENTRIES", 11, true},
        {"TAGS +/-", 13, true},
        {"VAL UPD", 8, true},
        {"RUNS/S", 7, true},
        {"AVG/RUN", 8, true},
    };
    vector<string> header;
    for (auto& c : cols) header.push_back(c.header);
    put_row(w, y++, width, cols, header, A_REVERSE);

    if (kinds.empty()) {
        put(w, y++, 0, width, "No filters reported. Install filters, or the agents may predate this pando_top.");
    }

    int first = y;
    int max_scroll = max(0, (int)kinds.size() - (height - first));
    ui.scroll = min(ui.scroll, max_scroll);
    int idx = 0;
    for (auto& [name, kind] : kinds) {
        if (idx++ < ui.scroll) continue;
        if (y >= height) break;
        FilterCounters c;
        auto it = counters.find(name);
        if (it != counters.end()) c = it->second;
        auto r = rates.find(name);
        string avg = c.ran > 0 ? fmt_duration_ms(c.run_ns / c.ran / 1000000) : "-";
        if (c.ran > 0 && c.run_ns / c.ran < 1000000) avg = to_string(c.run_ns / c.ran / 1000) + "us";
        put_row(w, y++, width, cols, {
            name,
            pass_kind_name(kind),
            fmt_count(c.checked, 9),
            fmt_count(c.ran, 9),
            fmt_count(c.entries_out, 11),
            fmt_count(c.tags_added, 6) + "/" + fmt_count(c.tags_removed, 6),
            fmt_count(c.val_updates, 8),
            fmt_rate(r == rates.end() ? 0.0 : r->second),
            avg,
        });
    }
    for (; y < height; ++y) put(w, y, 0, width, "");
}

/** @brief Filters x passes grid, newest pass on the right */
static void draw_passes(WINDOW* w, PandoTop& pp, UiState& ui, int height, int width) {
    // Which passes to show: every recorded one, plus the live one
    vector<uint64_t> pass_ids;
    for (auto& [p, rec] : pp.passes()) pass_ids.push_back(p);
    uint64_t live = pp.mesh_pass();
    if (pp.pass_live(live) && (pass_ids.empty() || pass_ids.back() != live)) pass_ids.push_back(live);

    auto kinds = pp.filter_kinds();
    for (auto& [p, rec] : pp.passes())
        for (auto& [name, c] : rec.filters)
            kinds.emplace(name, rec.kind == PASS_GROUP ? PASS_GROUP : PASS_SINGLE);

    int y = 0;
    wattron(w, A_BOLD);
    put(w, y++, 0, width, ui.outputs ? "New entries per pass, summed over the mesh" : "Entries each filter ran on, per pass, summed over the mesh");
    wattroff(w, A_BOLD);

    // Barrier / stragglers
    size_t waiting = 0, n = 0;
    auto late = stragglers(pp, &waiting, &n);
    string bl;
    if (!late.empty()) {
        bl = "Barrier: " + to_string(waiting) + "/" + to_string(n) + " agents waiting; still working:";
        for (auto a : late) bl += " " + addr_str(a);
    } else if (n > 0 && waiting == n) {
        bl = "Barrier: all " + to_string(n) + " agents arrived";
    }
    wattron(w, COLOR_PAIR(C_YELLOW));
    put(w, y++, 0, width, bl);
    wattroff(w, COLOR_PAIR(C_YELLOW));
    y++;

    if (pass_ids.empty()) {
        put(w, y++, 0, width, "No passes seen yet (passes that ended before pando_top started are not shown).");
        for (; y < height; ++y) put(w, y, 0, width, "");
        return;
    }

    int name_w = filter_name_width(kinds, width);
    const int cell_w = 9;
    int fit = max(1, (width - name_w - 1) / (cell_w + 1));
    int npass = pass_ids.size();
    ui.hscroll = max(0, min(ui.hscroll, npass - fit));
    int last = npass - ui.hscroll;          // exclusive
    int first = max(0, last - fit);

    vector<Col> cols { {"", name_w, false} };
    for (int i = first; i < last; ++i) cols.push_back({"", cell_w, true});

    auto row = [&](const string& label, auto cell_fn, attr_t attr = A_NORMAL) {
        vector<string> cells { label };
        for (int i = first; i < last; ++i) cells.push_back(cell_fn(pass_ids[i]));
        put_row(w, y++, width, cols, cells, attr);
    };

    map<uint64_t, map<string, FilterCounters>> counters;
    for (int i = first; i < last; ++i) counters[pass_ids[i]] = pp.pass_counters(pass_ids[i]);

    row(first > 0 ? "PASS  <-" : "PASS", [&](uint64_t p) {
        return to_string(p) + (pp.pass_live(p) ? "*" : "");
    }, A_REVERSE);
    row("type", [&](uint64_t p) {
        auto it = pp.passes().find(p);
        if (it != pp.passes().end()) return string(pass_kind_name(it->second.kind));
        for (auto& [a, ai] : pp.infos())
            if (ai.has_progress && ai.progress.pass == p && ai.progress.pass_kind != PASS_NONE)
                return string(pass_kind_name(ai.progress.pass_kind));
        return string("-");
    });
    row("duration", [&](uint64_t p) {
        auto it = pp.passes().find(p);
        if (it == pp.passes().end() || pp.pass_live(p)) return string("...");
        return fmt_duration_ms(it->second.max_duration_ms);
    });

    int filter_rows = height - y - 1;
    int max_scroll = max(0, (int)kinds.size() - filter_rows);
    ui.scroll = min(ui.scroll, max_scroll);
    int idx = 0;
    for (auto& [name, kind] : kinds) {
        if (idx++ < ui.scroll) continue;
        if (y >= height - 1) break;
        row(name, [&](uint64_t p) {
            auto& m = counters[p];
            auto it = m.find(name);
            if (it == m.end()) return string("-");
            return fmt_count(ui.outputs ? it->second.entries_out : it->second.ran, cell_w);
        });
    }
    for (; y < height - 1; ++y) put(w, y, 0, width, "");
    put(w, height - 1, 0, width, "* = still running on some agents" +
        string(pp.passes().empty() || pp.passes().begin()->first > 1 ? "; passes before pando_top started are not shown" : ""));
}

static string describe_event(const StageEvent& e) {
    string s = fmt_clock_ms(e.time_ms) + "  " + pad(addr_str(e.agent), 16) + " ";
    if (e.kind == EVENT_PASS_END) {
        s += "pass " + to_string(e.summary.pass) + " (" + pass_kind_name(e.summary.kind) + ") done in " +
            fmt_duration_ms(e.summary.duration_ms);
        if (e.summary.stopped_early) s += ", stopped early at the run limit";
        string ran;
        for (auto& [name, c] : e.summary.filters)
            if (c.ran > 0) ran += " " + name + "=" + fmt_count(c.ran);
        s += ran.empty() ? "; no filters ran" : ";" + ran;
    } else {
        s += "pass " + to_string(e.summary.pass) + "  " + state_name(e.from_state) + " -> " + state_name(e.to_state);
    }
    return s;
}

/** @brief The event log, newest at the bottom */
static void draw_events(WINDOW* w, PandoTop& pp, UiState& ui, int height, int width) {
    auto& ev = pp.events();
    int n = ev.size();
    ui.scroll = max(0, min(ui.scroll, n - height));
    int end = n - ui.scroll;        // exclusive
    int start = max(0, end - height);
    int y = 0;
    if (n == 0) put(w, y++, 0, width, "No events yet; they appear as agents change stage.");
    for (int i = start; i < end; ++i) {
        bool pass_end = ev[i].kind == EVENT_PASS_END;
        if (pass_end) wattron(w, A_BOLD);
        put(w, y++, 0, width, describe_event(ev[i]));
        if (pass_end) wattroff(w, A_BOLD);
    }
    for (; y < height; ++y) put(w, y, 0, width, "");
}

/** @brief The header (shared by every screen) */
class Header {
    private:
        PandoTop& pp_;
        WINDOW *w_header_, *w_stats_, *w_up_, *w_hl_;
        int stats_w_;
        int status_ = 0;
    public:
        Header(PandoTop& pp, int cols) : pp_(pp) {
            w_header_ = newwin(3, 13, 1, 1);
            box(w_header_, 0, 0);
            wattron(w_header_, COLOR_PAIR(C_RED));
            mvwaddstr(w_header_, 1, 2, "PANDO TOP");
            wattroff(w_header_, COLOR_PAIR(C_RED));

            stats_w_ = cols-14-7;
            w_stats_ = newwin(5, stats_w_, 1, 14);
            box(w_stats_, 0, 0);
            mvwaddstr(w_stats_, 1, 2, "Mesh size     : ");
            mvwaddstr(w_stats_, 2, 2, "Total DB size : ");

            w_up_ = newwin(3, 5, 1, cols-7);
            box(w_up_, 0, 0);

            w_hl_ = newwin(1, cols, BODY_Y-1, 0);
            for (int c = 0; c < cols; ++c)
                wprintw(w_hl_, "▄");
        }
        ~Header() {
            delwin(w_hl_);
            delwin(w_up_);
            delwin(w_stats_);
            delwin(w_header_);
        }
        Header(const Header&) = delete;
        Header& operator=(const Header&) = delete;

        void draw() {
            size_t known = 0, db_size = 0;
            for (addr_t a : pp_.agents()) {
                if (auto ai = pp_.info(a)) {
                    ++known;
                    db_size += ai->db_size;
                }
            }
            size_t full = pp_.agents().size();
            int vw = stats_w_ - 2 - 16 - 1;
            put(w_stats_, 1, 18, vw, known != full ? to_string(known) + " / " + to_string(full) : to_string(known));
            put(w_stats_, 2, 18, vw, to_string(db_size));
            put(w_stats_, 3, 2, stats_w_ - 4, progress_line(pp_));

            static const char spin[] = { '-', '\\', '|', '/' };
            wattron(w_up_, COLOR_PAIR(C_BLUE));
            mvwaddch(w_up_, 1, 2, spin[status_]);
            wattroff(w_up_, COLOR_PAIR(C_BLUE));
            status_ = (status_ + 1) % 4;

            wnoutrefresh(w_header_);
            wnoutrefresh(w_stats_);
            wnoutrefresh(w_up_);
            wnoutrefresh(w_hl_);
        }
};

/** @brief The tab bar, on stdscr */
static void draw_tabs(const UiState& ui, int cols) {
    move(BODY_Y-2, 0);
    clrtoeol();
    int x = 1;
    for (int s = 0; s < NUM_SCREENS; ++s) {
        string label = " " + to_string(s+1) + " " + screen_names[s] + " ";
        if (x + (int)label.size() >= cols) break;
        if (s == ui.screen) attron(A_REVERSE);
        mvaddstr(BODY_Y-2, x, label.c_str());
        if (s == ui.screen) attroff(A_REVERSE);
        x += label.size() + 1;
    }
    const char* quit = " q Quit";
    if (x + (int)strlen(quit) < cols) mvaddstr(BODY_Y-2, x, quit);
}

static void draw_footer(const UiState& ui, PandoTop& pp, size_t hidden_agents, int rows, int cols) {
    string s;
    if (ui.screen == SCREEN_AGENTS) {
        if (hidden_agents > 0)
            s = "+" + to_string(hidden_agents) + " agents not shown (enlarge the window to see them)";
        else
            s = "blue: fresh  magenta: >10s  red: >60s since last report  yellow: holding up a barrier";
    } else if (ui.screen == SCREEN_FILTERS) {
        s = string("t: show ") + (ui.totals ? "current pass" : "all passes") + "   up/down: scroll";
    } else if (ui.screen == SCREEN_PASSES) {
        s = string("o: show ") + (ui.outputs ? "runs" : "new entries") + "   left/right: older/newer passes   up/down: scroll";
    } else if (ui.screen == SCREEN_EVENTS) {
        s = "up/down, PgUp/PgDn: scroll   End: newest";
    }
    if (pp.bad_msgs() > 0) s += "   (" + to_string(pp.bad_msgs()) + " malformed messages ignored)";
    move(rows-1, 0);
    clrtoeol();
    mvaddnstr(rows-1, 1, s.c_str(), cols-2);
}

enum InputResult { INPUT_NONE, INPUT_REDRAW, INPUT_REBUILD };

/** @brief Apply a key press to the UI state */
static InputResult handle_key(int ch, UiState& ui, int page) {
    Screen before = ui.screen;
    switch (ch) {
        case ERR: return INPUT_NONE;
        case KEY_RESIZE: return INPUT_REBUILD;
        case 'q': case 'Q':
            *g_shutdown = true;
            return INPUT_REBUILD;
        case '1': case '2': case '3': case '4':
            ui.screen = (Screen)(ch - '1');
            break;
        case '\t':
            ui.screen = (Screen)((ui.screen + 1) % NUM_SCREENS);
            break;
        case KEY_BTAB:
            ui.screen = (Screen)((ui.screen + NUM_SCREENS - 1) % NUM_SCREENS);
            break;
        case 't': ui.totals = !ui.totals; return INPUT_REDRAW;
        case 'o': ui.outputs = !ui.outputs; return INPUT_REDRAW;
        // The event log scrolls back from the newest entry, so up is +1
        case KEY_UP: ui.scroll += ui.screen == SCREEN_EVENTS ? 1 : -1; break;
        case KEY_DOWN: ui.scroll += ui.screen == SCREEN_EVENTS ? -1 : 1; break;
        case KEY_PPAGE: ui.scroll += ui.screen == SCREEN_EVENTS ? page : -page; break;
        case KEY_NPAGE: ui.scroll += ui.screen == SCREEN_EVENTS ? -page : page; break;
        case KEY_HOME: ui.scroll = ui.screen == SCREEN_EVENTS ? 1 << 30 : 0; break;
        case KEY_END: ui.scroll = ui.screen == SCREEN_EVENTS ? 0 : 1 << 30; break;
        case KEY_LEFT: ++ui.hscroll; break;
        case KEY_RIGHT: --ui.hscroll; break;
        default: return INPUT_NONE;
    }
    ui.scroll = max(0, ui.scroll);
    ui.hscroll = max(0, ui.hscroll);
    if (ui.screen != before) {
        ui.reset_scroll();
        return INPUT_REBUILD;
    }
    return INPUT_REDRAW;
}

// Draw one layout for the current terminal size and screen, and run until the
// terminal is resized, the screen changes, or we are asked to quit. All
// windows are created here and deleted on return, so the caller can simply
// call it again.
void top(PandoTop& pp, UiState& ui) {
    // clear() rather than erase(): forces a full repaint, wiping the old layout
    clear();
    refresh();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // Too small for the layout: say so and wait for a resize instead of
    // creating windows that would not fit
    if (rows < MIN_ROWS || cols < MIN_COLS) {
        mvprintw(0, 0, "Window too small (%dx%d, need %dx%d)", cols, rows, MIN_COLS, MIN_ROWS);
        refresh();
        while (!*g_shutdown) {
            int ch = getch();
            if (ch == 'q' || ch == 'Q') *g_shutdown = true;
            int r, c;
            getmaxyx(stdscr, r, c);
            if (ch == KEY_RESIZE || r != rows || c != cols) break;
            while (pp.recv_msg(0)) {}
        }
        return;
    }

    Header header { pp, cols };
    int body_h = rows - BODY_Y - 1;
    unique_ptr<AgentsView> agents;
    WINDOW* body = nullptr;
    if (ui.screen == SCREEN_AGENTS) agents = make_unique<AgentsView>(pp, rows, cols);
    else body = newwin(body_h, cols, BODY_Y, 0);

    auto next_draw = steady::now();
    bool dirty = true;
    while (!*g_shutdown) {
        // Input; getch() waits at most 10 ms, pacing the loop
        InputResult in = handle_key(getch(), ui, max(1, body_h - 4));
        if (in == INPUT_REBUILD) break;
        if (in == INPUT_REDRAW) dirty = true;

        // A resize may be seen before KEY_RESIZE is read
        int r, c;
        getmaxyx(stdscr, r, c);
        if (r != rows || c != cols) break;

        // Drain pending messages without blocking
        for (size_t ctr = 0; ctr < 10000 && pp.recv_msg(0); ++ctr) {}
        if (pp.take_changed()) dirty = true;

        // Redraw on input, or at most 4 times a second for new data (the
        // spinner turning shows pando_top itself is alive)
        auto now = steady::now();
        if (!(in == INPUT_REDRAW || (now >= next_draw && dirty) || now >= next_draw + chrono::seconds(1))) continue;
        next_draw = now + chrono::milliseconds(250);
        dirty = false;

        header.draw();
        size_t hidden = 0;
        if (agents) {
            agents->draw();
            hidden = agents->hidden();
        } else if (ui.screen == SCREEN_FILTERS) {
            draw_filters(body, pp, ui, body_h, cols);
        } else if (ui.screen == SCREEN_PASSES) {
            draw_passes(body, pp, ui, body_h, cols);
        } else if (ui.screen == SCREEN_EVENTS) {
            draw_events(body, pp, ui, body_h, cols);
        }
        if (body != nullptr) wnoutrefresh(body);
        draw_tabs(ui, cols);
        draw_footer(ui, pp, hidden, rows, cols);
        wnoutrefresh(stdscr);
        doupdate();
    }

    if (body != nullptr) delwin(body);
}

int main_(int argc, char **argv) {
    if (argc < 2) {
        cerr << "Usage: pando_top mesh-addr\n"
            "\n"
            "Parameters:\n"
            "  mesh-addr : an address in the mesh to join\n"
            "\n"
            "Addresses are of the form: IPv4-string,ID\n"
            "  IPv4-string : a period separated IP address, e.g., 1.2.3.4\n"
            "  ID : a small integer between 0 and 199\n"
            "\n"
            "Keys: 1-4 or Tab switch screens (Agents, Filters, Passes, Events), q quits\n"
            ;
        return 1;
    }

    elga::ZMQAddress mesh_addr = get_zmq_addr(argv[1]);

    PandoTop pp { };
    pp.add_neighbor(mesh_addr.serialize(), false);
    pp.request_mesh(mesh_addr.serialize());

    volatile sig_atomic_t shutdown = false;
    g_shutdown = &shutdown;
    set_handler();

    // Set up the terminal once; top() rebuilds its windows as needed
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    keypad(stdscr, true);
    noecho();
    start_color();
    curs_set(0);
    timeout(10);    // getch() waits at most 10 ms, pacing the update loop

    init_pair(C_RED, COLOR_RED, COLOR_BLACK);
    init_pair(C_BLUE, COLOR_BLUE, COLOR_BLACK);
    init_pair(C_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(C_YELLOW, COLOR_YELLOW, COLOR_BLACK);

    UiState ui;
    while (!shutdown)
        top(pp, ui);

    endwin();
    return 0;
}

int main(int argc, char **argv) {
    elga::ZMQChatterbox::Setup();

    int ret;

    try {
        ret = main_(argc, argv);
    } catch(...) {
        elga::ZMQChatterbox::Teardown();
        throw;
    }

    elga::ZMQChatterbox::Teardown();

    return ret;
}
