#include <exception>
#include <locale.h>
#include <ncurses.h>
#include <string>
#include <sys/ioctl.h>

#include "address.hpp"
#include "par_db.hpp"

#include "util.hpp"


using namespace std;
using namespace pando;

class PandoTop : public PandoParticipant {
    private:
        unordered_map<addr_t, tuple<time_t, size_t>> db_state_;
    public:
        PandoTop() : PandoParticipant(elga::ZMQAddress{}, false) {
            sub(STATS);
        }
        void process_msg([[maybe_unused]] msg_type_t type, [[maybe_unused]] zmq_socket_t sock, [[maybe_unused]] const char *data, [[maybe_unused]] const char *end) {
            if (type == STATS) {
                addr_t agent;
                size_t size;
                unpack_single(data, agent);
                unpack_single(data, size);
                time_t now = time(nullptr);
                db_state_[agent] = {now, size};
            } else
                recv_unknown_msg();
        }
        tuple<time_t, size_t> get_state(addr_t addr) {
            if (db_state_.count(addr) == 0) return {0, 0};
            return db_state_[addr];
        }
};

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

struct WinPos {
    WINDOW* w;
    int y;
    int x;
    WinPos(WINDOW* nw, int ny, int nx) : w(nw), y(ny), x(nx) { }
    WinPos() : w(nullptr), y(0), x(0) { }
};

struct AgentData {
    WINDOW* w;
    size_t db_size;
    time_t last_seen;
    string str;
    AgentData() : w(nullptr), db_size(0), last_seen(0) { }
};

// Smallest terminal the layout fits in: the stats box needs 31 columns
// ("Total DB size : " plus an 11-digit size) after the 14-column header and
// the 7-column spinner. Agent boxes start at row 7 and are 3 rows tall, and
// the last row is kept for the "not shown" note, so one row of agents needs 11.
static const int MIN_ROWS = 11;
static const int MIN_COLS = 52;

class PandoData {
    private:
        PandoTop& pp_;
        WinPos mesh_, db_, up_;
        int status_;
        unordered_map<addr_t, AgentData> agents_;
        size_t hidden_agents_;

        inline static WinPos get_pos_(WINDOW *w) {
            int y, x;
            getyx(w, y, x);
            return {w, y, x};
        }

        const int blk_h_ = 3;
        const int blk_w_ = 22;
        const int blk_pad_ = 1;

        int x_, y_;
        int rows_, cols_;

    public:
        PandoData(PandoTop& pp, int rows, int cols) :
                pp_(pp), status_(0), hidden_agents_(0), x_(2), y_(7),
                rows_(rows), cols_(cols) {
        }

        ~PandoData() {
            for (auto& [addr, ad] : agents_)
                if (ad.w != nullptr) delwin(ad.w);
        }

        PandoData(const PandoData&) = delete;
        PandoData& operator=(const PandoData&) = delete;

        void set_mesh_pos(WINDOW* w) {
            mesh_ = get_pos_(w);
        }
        void write_mesh() {
            if (mesh_.w == nullptr) return;
            size_t known = agents_.size();
            size_t full = pp_.agents().size();
            if (known != full)
                mvwprintw(mesh_.w, mesh_.y, mesh_.x, "%-4ld / %-4ld", known, full);
            else
                mvwprintw(mesh_.w, mesh_.y, mesh_.x, "%-4ld%7s", known, " ");
        }

        void set_db_pos(WINDOW* w) {
            db_ = get_pos_(w);
        }
        void write_db() {
            if (db_.w == nullptr) return;
            size_t db_size = 0;
            for (auto& [addr, ad] : agents_) {
                db_size += ad.db_size;
            }

            mvwprintw(db_.w, db_.y, db_.x, "%-11ld", db_size);
        }

        void set_up_pos(WINDOW* w) {
            up_ = get_pos_(w);
        }
        void write_up() {
            if (up_.w == nullptr) return;
            wmove(up_.w, up_.y, up_.x);
            wattron(up_.w, COLOR_PAIR(2));
            char status_char = 'X';
            if (status_ % 4 == 0)
                status_char = '-';
            else if (status_ % 4 == 1)
                status_char = '\\';
            else if (status_ % 4 == 2)
                status_char = '|';
            else if (status_ % 4 == 3)
                status_char = '/';
            ++status_;
            if (status_ == 4) status_ = 0;
            waddch(up_.w, status_char);
            wattroff(up_.w, COLOR_PAIR(2));

            wrefresh(up_.w);
        }

        void write_stats() {
            write_mesh();
            write_db();
            if (db_.w != nullptr) wrefresh(db_.w);
        }

        // Agents that do not fit on screen get no window; report how many
        void write_hidden() {
            move(rows_-1, 0);
            clrtoeol();
            if (hidden_agents_ > 0)
                mvprintw(rows_-1, 2, "+%zu agents not shown (enlarge the window to see them)", hidden_agents_);
            wnoutrefresh(stdscr);
        }

        void update() {
            // Process any messages, and register / create windows for all new
            // agents
            size_t max_recvs = pp_.agents().size()*2;
            for (size_t ctr = 0; ctr < max_recvs && pp_.recv_msg(); ++ctr) {}

            // Now, find any new agents and provide them windows as appropriate
            for (addr_t addr : pp_.agents()) {
                if (agents_.count(addr) == 0) {
                    // This is a new agent
                    AgentData ad;

                    elga::ZMQAddress elga_addr {addr};
                    ad.str = elga_addr.get_addr_str();

                    // Create a new window for it, if one fits (the last row is
                    // kept free for the "not shown" note)
                    if (x_ + blk_w_ + blk_pad_ >= cols_) {
                        // Move to the next line
                        y_ += blk_h_;
                        x_ = 2;
                    }
                    if (y_ + blk_h_ <= rows_ - 1 && x_ + blk_w_ <= cols_)
                        ad.w = newwin(blk_h_, blk_w_, y_, x_);
                    if (ad.w != nullptr) {
                        x_ += blk_w_ + blk_pad_;
                        wattron(ad.w, COLOR_PAIR(3));
                        box(ad.w, 0, 0);
                        wattroff(ad.w, COLOR_PAIR(3));
                        wrefresh(ad.w);
                    } else {
                        ++hidden_agents_;
                    }

                    agents_[addr] = move(ad);
                }

                // Update the state
                auto [ls, size] = pp_.get_state(addr);
                agents_[addr].db_size = size;
                agents_[addr].last_seen = ls;

                auto w = agents_[addr].w;
                if (w == nullptr) continue;

                auto age = time(nullptr) - agents_[addr].last_seen;
                if (age > 999) age = 999;

                if (age > 60) {
                    wattron(w, COLOR_PAIR(1));
                    box(w, 0, 0);
                    wattroff(w, COLOR_PAIR(1));
                } else if (age < 10) {
                    wattron(w, COLOR_PAIR(2));
                    box(w, 0, 0);
                    wattroff(w, COLOR_PAIR(2));
                } else {
                    wattron(w, COLOR_PAIR(3));
                    box(w, 0, 0);
                    wattroff(w, COLOR_PAIR(3));
                }

                if (age > 4) {
                    string age_s = to_string(age);
                    size_t age_s_len = age_s.size();

                    mvwprintw(w, 0, blk_w_-age_s_len-1, "%s", age_s.c_str());
                    if (age == 999)
                        wprintw(w, "+");
                }
                string addr_str = agents_[addr].str;
                mvwprintw(w, 0, 1, "%s", addr_str.c_str());
                mvwprintw(w, 1, 2, "DB size : %-8ld", agents_[addr].db_size);
                wrefresh(w);
            }

            // Write updates
            write_stats();
            write_up();
            write_hidden();
            doupdate();
        }
};

// Wait for the next input event or timeout. Returns true if the terminal was
// resized. ncurses reports resizes as KEY_RESIZE from getch(), and only then
// updates LINES/COLS, so reading input is what keeps the size current.
static bool wait_for_resize(int rows, int cols) {
    int ch = getch();
    if (ch == KEY_RESIZE) return true;
    int cur_rows, cur_cols;
    getmaxyx(stdscr, cur_rows, cur_cols);
    return cur_rows != rows || cur_cols != cols;
}

// Draw one layout for the current terminal size and run until the terminal is
// resized or Ctrl+C is pressed. All windows are created here and deleted on
// return, so the caller can simply call it again after a resize.
void top(PandoTop& pp) {
    vector<WINDOW*> wins;

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
        while (!*g_shutdown && !wait_for_resize(rows, cols)) {}
        return;
    }

    // Keep track of the data to present
    PandoData pd {pp, rows, cols};

    // Build the windows
    WINDOW *w_header;
    w_header = newwin(3, 13, 1, 1);
    wins.push_back(w_header);
    box(w_header, 0, 0);
    wattron(w_header, COLOR_PAIR(1));
    mvwprintw(w_header, 1, 2, "PANDO TOP");
    wattroff(w_header, COLOR_PAIR(1));

    wrefresh(w_header);

    WINDOW *w_stats;
    w_stats = newwin(4, cols-14-7, 1, 14);
    wins.push_back(w_stats);
    box(w_stats, 0, 0);

    mvwprintw(w_stats, 1, 2, "Mesh size     : ");
    pd.set_mesh_pos(w_stats);
    mvwprintw(w_stats, 2, 2, "Total DB size : ");
    pd.set_db_pos(w_stats);

    pd.write_stats();

    WINDOW *w_up = newwin(3, 5, 1, cols-7);
    wins.push_back(w_up);
    box(w_up, 0, 0);
    wmove(w_up, 1, 2);
    pd.set_up_pos(w_up);

    pd.write_up();

    WINDOW *w_hl = newwin(1, cols, 5, 0);
    wins.push_back(w_hl);
    for (int c = 0; c < cols; ++c)
        wprintw(w_hl, "▄");
    wrefresh(w_hl);

    while (!*g_shutdown && !wait_for_resize(rows, cols))
        pd.update();

    for (auto revit = wins.rbegin(); revit != wins.rend(); ++revit)
        if (*revit != nullptr) delwin(*revit);
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

    // Set up the terminal once; top() rebuilds its windows after each resize
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    keypad(stdscr, true);
    noecho();
    start_color();
    curs_set(0);
    timeout(10);    // getch() waits at most 10 ms, pacing the update loop

    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_BLUE, COLOR_BLACK);
    init_pair(3, COLOR_MAGENTA, COLOR_BLACK);

    while (!shutdown)
        top(pp);

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

