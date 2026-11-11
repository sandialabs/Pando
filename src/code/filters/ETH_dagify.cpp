#include "helpers.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace std;
using namespace pando;

#define FILTER_NAME "ETH_dagify"

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

static inline string lower_trim_copy(string_view s) {
    s = trim_view(s);
    string out(s);
    transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(tolower(c));
    });
    return out;
}

static inline uint32_t parse_u32(string_view s) {
    s = trim_view(s);

    uint32_t value = 0;

    for (char c : s) {
        value = static_cast<uint32_t>(value * 10 + static_cast<uint32_t>(c - '0'));
    }

    return value;
}

static inline uint32_t hex_nibble(char c) {
    if (c <= '9') return static_cast<uint32_t>(c - '0');
    if (c <= 'F') return static_cast<uint32_t>(10 + c - 'A');
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

static inline uint32_t addr_b_key_from_low32(uint32_t low32) {
    return low32 >> 16;
}

static inline uint32_t addr_c_key_from_low32(uint32_t low32) {
    return low32 & 0xFFFF;
}

void ETH_dagify(const DBAccess *access, const char* filter_fail_tag, const char* filter_done_tag) {
    try {
        string_view row = access->value == nullptr ? string_view("") : string_view(access->value);

        vector<string> fields;
        fields.reserve(16);

        string field;
        field.reserve(row.size());

        bool in_quotes = false;

        for (size_t i = 0; i < row.size(); ++i) {
            char c = row[i];

            if (in_quotes) {
                if (c == '"') {
                    if (i + 1 < row.size() && row[i + 1] == '"') {
                        field.push_back('"');
                        ++i;
                    } else {
                        in_quotes = false;
                    }
                } else {
                    field.push_back(c);
                }
            } else {
                if (c == '"') {
                    in_quotes = true;
                } else if (c == ',') {
                    fields.push_back(field);
                    field.clear();
                } else if (c == '\n' || c == '\r') {
                    break;
                } else {
                    field.push_back(c);
                }
            }
        }

        fields.push_back(field);

        uint32_t current_block = parse_u32(fields[3]);
        string from_address = lower_trim_copy(fields[5]);
        string to_address = lower_trim_copy(fields[6]);

        uint32_t from_low32 = addr_low32(from_address);
        uint32_t from_b_key = addr_b_key_from_low32(from_low32);
        uint32_t from_c_key = addr_c_key_from_low32(from_low32);
        uint32_t from_lower_16 = addr_c_key_from_low32(from_low32);

        dbkey_t from_destination_key = {
            ETH_DEST_ROARING_BITMAP_KEY,
            from_b_key,
            from_c_key
        };

        const char* bitmap_value = nullptr;

        try {
            access->get_entry_by_key.run(
                &access->get_entry_by_key,
                from_destination_key,
                &bitmap_value
            );
        } catch (...) {
            bitmap_value = nullptr;
        }

        int64_t previous_destination_block = -1;

        if (bitmap_value != nullptr) {
            string_view entry_value(bitmap_value);
            string_view target(from_address.data(), from_address.size());

            size_t line_start = 0;

            while (line_start < entry_value.size()) {
                size_t line_end = entry_value.find('\n', line_start);

                if (line_end == string_view::npos) {
                    line_end = entry_value.size();
                }

                string_view line = trim_view(entry_value.substr(line_start, line_end - line_start));
                line_start = line_end + 1;

                if (line.size() <= target.size()) {
                    continue;
                }

                if (line[target.size()] != '|') {
                    continue;
                }

                if (line.substr(0, target.size()) != target) {
                    continue;
                }

                size_t rb_key_start = target.size() + 1;
                size_t colon_pos = line.find(':', rb_key_start);

                if (colon_pos == string_view::npos) {
                    continue;
                }

                uint32_t rb_key = parse_u32(line.substr(rb_key_start, colon_pos - rb_key_start));
                string_view values = line.substr(colon_pos + 1);

                size_t token_start = 0;

                while (token_start < values.size()) {
                    size_t token_end = values.find(',', token_start);

                    if (token_end == string_view::npos) {
                        token_end = values.size();
                    }

                    uint32_t rb_value = parse_u32(values.substr(token_start, token_end - token_start));
                    uint32_t block = static_cast<uint32_t>((rb_key << 16) | rb_value);

                    if (block < current_block && static_cast<int64_t>(block) > previous_destination_block) {
                        previous_destination_block = static_cast<int64_t>(block);
                    }

                    token_start = token_end + 1;
                }
            }
        }

        if (previous_destination_block >= 0) {
            fields[5] = from_address + "_" + to_string(previous_destination_block);
        } else {
            fields[5] = from_address + "_init";
        }

        dbkey_t dag_key;

        if (to_address.empty() || to_address == "0x") {
            fields[6] = to_address + "_" + to_string(current_block);
            dag_key = {ETH_DAG_KEY, from_lower_16, 0};
        } else {
            uint32_t to_low32 = addr_low32(to_address);
            uint32_t to_lower_16 = addr_c_key_from_low32(to_low32);

            fields[6] = to_address + "_" + to_string(current_block);
            dag_key = {ETH_DAG_KEY, from_lower_16, to_lower_16};
        }

        string new_row_text;

        size_t estimated_size = fields.size();

        for (const string& f : fields) {
            estimated_size += f.size();
        }

        new_row_text.reserve(estimated_size);

        auto append_csv_field = [&](const string& f) {
            bool quote = false;

            for (char c : f) {
                if (c == '"' || c == ',' || c == '\n' || c == '\r') {
                    quote = true;
                    break;
                }
            }

            if (!quote) {
                new_row_text += f;
                return;
            }

            new_row_text.push_back('"');

            for (char c : f) {
                if (c == '"') {
                    new_row_text.push_back('"');
                    new_row_text.push_back('"');
                } else {
                    new_row_text.push_back(c);
                }
            }

            new_row_text.push_back('"');
        };

        for (size_t i = 0; i < fields.size(); ++i) {
            if (i) {
                new_row_text.push_back(',');
            }

            append_csv_field(fields[i]);
        }

        const char* new_tags[] = {"ETH", "dag-edge", ""};

        access->make_new_entry.run(
            &access->make_new_entry,
            new_tags,
            new_row_text.c_str(),
            dag_key
        );

        access->add_tag.run(&access->add_tag, filter_done_tag);
    } catch (const exception& e) {
        fail_(access, filter_fail_tag, FILTER_NAME, " ", e.what());
    } catch (...) {
        fail_(access, filter_fail_tag, FILTER_NAME, " unknown exception");
    }
}

bool should_run_ETH_dagify(const DBAccess* access, const char* filter_fail_tag, const char* filter_done_tag) {
    auto tags = access->tags;
    bool found_eth = false;
    bool found_tx_csv = false;

    while ((*tags)[0] != '\0') {
        if (strcmp(*tags, filter_done_tag) == 0 || strcmp(*tags, filter_fail_tag) == 0) {
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
    return ETH_dagify(access, filter_fail_tag, filter_done_tag);
}

extern "C" {
    extern void run(void* access) {
        run_((DBAccess*)access);
    }

    extern bool should_run(const DBAccess* access) {
        return should_run_ETH_dagify(access, filter_fail_tag, filter_done_tag);
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
