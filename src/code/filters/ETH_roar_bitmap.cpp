#include "helpers.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

using namespace std;
using namespace pando;

#define FILTER_NAME "ETH_roar_bitmap"

static const char* filter_done_tag = FILTER_NAME ":done";
static const char* filter_fail_tag = FILTER_NAME ":fail";

template<typename ...Args>
void fail_(const DBAccess *access, const char* filter_fail_tag, const char* filter_name, Args && ...args) {
    cerr << "[ERROR] unable to run filter: " << filter_name;
    (cerr << ... << args);
    cerr << endl;
    access->add_tag.run(&access->add_tag, filter_fail_tag);
}

static inline string_view trim_view(string_view s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }

    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }

    return s;
}

static inline uint32_t parse_u32(string_view s) {
    s = trim_view(s);

    uint32_t value = 0;

    for (char c : s) {
        value = static_cast<uint32_t>(value * 10 + static_cast<uint32_t>(c - '0'));
    }

    return value;
}

static inline string lower_trim_copy(string_view s) {
    s = trim_view(s);

    string out(s);

    transform(
        out.begin(),
        out.end(),
        out.begin(),
        [](unsigned char c) {
            return static_cast<char>(tolower(c));
        }
    );

    return out;
}

static inline uint32_t hex_nibble(char c) {
    if (c <= '9') {
        return static_cast<uint32_t>(c - '0');
    }

    return static_cast<uint32_t>(10 + c - 'a');
}

static inline uint32_t addr_low32(string_view addr) {
    addr = trim_view(addr);

    if (addr.size() >= 2 && addr[0] == '0' && addr[1] == 'x') {
        addr.remove_prefix(2);
    }

    size_t start = addr.size() > 8 ? addr.size() - 8 : 0;
    uint32_t low32 = 0;

    for (size_t i = start; i < addr.size(); ++i) {
        low32 = static_cast<uint32_t>((low32 << 4) | hex_nibble(addr[i]));
    }

    return low32;
}

void eth_roar_bitmap(const DBAccess *access, const char* filter_fail_tag, const char* filter_done_tag) {
    try {
        string_view row(access->value);

        size_t block_start = 0;
        size_t block_end = 0;
        size_t to_start = 0;
        size_t to_end = 0;

        size_t field_index = 0;
        size_t field_start = 0;
        bool in_quotes = false;

        for (size_t i = 0; i < row.size(); ++i) {
            char c = row[i];

            if (in_quotes) {
                if (c == '"') {
                    if (i + 1 < row.size() && row[i + 1] == '"') {
                        ++i;
                    } else {
                        in_quotes = false;
                    }
                }

                continue;
            }

            if (c == '"') {
                in_quotes = true;
                continue;
            }

            if (c == ',' || c == '\n' || c == '\r') {
                if (field_index == 3) {
                    block_start = field_start;
                    block_end = i;
                } else if (field_index == 6) {
                    to_start = field_start;
                    to_end = i;
                    break;
                }

                if (c == '\n' || c == '\r') {
                    break;
                }

                ++field_index;
                field_start = i + 1;
            }
        }

        if (field_index == 3) {
            block_start = field_start;
            block_end = row.size();
        } else if (field_index == 6) {
            to_start = field_start;
            to_end = row.size();
        }

        uint32_t block_num = parse_u32(row.substr(block_start, block_end - block_start));
        string to_address = lower_trim_copy(row.substr(to_start, to_end - to_start));

        if (!to_address.empty() && to_address != "0x") {
            uint32_t rb_key = block_num >> 16;
            uint32_t rb_value = block_num & 0xFFFF;

            uint32_t to_low32 = addr_low32(to_address);
            uint32_t to_b_key = to_low32 >> 16;
            uint32_t to_c_key = to_low32 & 0xFFFF;

            dbkey_t to_new_key = {ETH_DEST_ROARING_BITMAP_KEY, to_b_key, to_c_key};

            char rb_key_buf[11];
            char rb_value_buf[11];

            auto rb_key_result = to_chars(rb_key_buf, rb_key_buf + sizeof(rb_key_buf), rb_key);
            auto rb_value_result = to_chars(rb_value_buf, rb_value_buf + sizeof(rb_value_buf), rb_value);

            string to_new_value;
            to_new_value.reserve(
                to_address.size() +
                1 +
                static_cast<size_t>(rb_key_result.ptr - rb_key_buf) +
                1 +
                static_cast<size_t>(rb_value_result.ptr - rb_value_buf)
            );

            to_new_value += to_address;
            to_new_value.push_back('|');
            to_new_value.append(rb_key_buf, rb_key_result.ptr);
            to_new_value.push_back(':');
            to_new_value.append(rb_value_buf, rb_value_result.ptr);

//             const char* new_tags[] = {"ETH", "BITMAP", "MERGE_STRATEGY=KV", ""};
            const char* new_tags[] = {"ETH", "BITMAP", ""};

            access->make_new_entry.run(
                &access->make_new_entry,
                new_tags,
                to_new_value.c_str(),
                to_new_key
            );
        }

        access->add_tag.run(&access->add_tag, filter_done_tag);
    }
    catch (const exception& e) {
        fail_(access, filter_fail_tag, FILTER_NAME, " ", e.what());
    }
    catch (...) {
        fail_(access, filter_fail_tag, FILTER_NAME, " unknown exception");
    }
}

bool should_run_eth_roar_bitmap(const DBAccess* access, const char* filter_fail_tag, const char* filter_done_tag) {
    auto tags = access->tags;

    bool found_eth = false;
    bool found_tx_csv = false;

    while ((*tags)[0] != '\0') {
        if (strcmp(*tags, filter_done_tag) == 0) {
            return false;
        }

        if (strcmp(*tags, filter_fail_tag) == 0) {
            return false;
        }

        if (strcmp(*tags, "ETH") == 0) {
            found_eth = true;
        }

        if (strcmp(*tags, "tx_csv") == 0) {
            found_tx_csv = true;
        }

        ++tags;
    }

    return found_eth && found_tx_csv;
}

void run_(const DBAccess *access) {
    return eth_roar_bitmap(access, filter_fail_tag, filter_done_tag);
}

extern "C" {
    extern void run(void* access) {
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        return should_run_eth_roar_bitmap(access, filter_fail_tag, filter_done_tag);
    }

    extern const FilterInterface filter {
        filter_name: FILTER_NAME,
        filter_type: SINGLE_ENTRY,
        should_run: &should_run,
        init: nullptr,
        destroy: nullptr,
        run: &run
    };
}
