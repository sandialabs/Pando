#pragma once

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>

#include "dbentry.hpp"

#include "pack.hpp"
#include "chatterbox.hpp"
#include "consistenthasher.hpp"
#include "util.hpp"
#include "seq_db.hpp"

using namespace std;
using namespace elga;

namespace pando {

/** @brief Contains a client that can communicate with ParDB over the net */
class ParDBClient : ZMQChatterbox {
    private:
        /** @brief Contains the persistent connection to the server */
        ZMQRequester req_;

    public:
        /** @brief Start the client and connect to a ParDB */
        ParDBClient(ZMQAddress server_address) :
                ZMQChatterbox(ZMQAddress {}), req_(server_address, addr_) { }

        ParDBClient(string server_address) :
                ParDBClient(get_zmq_addr(server_address)) { }

        /** @brief Get the current number of neighbors */
        size_t num_neighbors() {
            req_.send(NUM_NEIGHBORS);
            ZMQMessage resp = req_.read();
            const char *resp_data = resp.data();
            size_t ret = unpack_single<size_t>(resp_data);
            return ret;
        }

        /** @brief Add a new neighbor to the server */
        void add_neighbor(ZMQAddress addr) {
            size_t msg_size = sizeof(msg_type_t)+sizeof(addr_t);
            char msg[msg_size];
            char *msg_ptr = msg;
            pack_msg(msg_ptr, ADD_NEIGHBOR);
            pack_single(msg_ptr, addr.serialize());
            req_.send(msg, msg_size);
            req_.wait_ack();
        }

        /** @brief Return the current ring size */
        size_t ring_size() {
            req_.send(RING_SIZE);
            ZMQMessage resp = req_.read();
            const char *resp_data = resp.data();
            size_t ret = unpack_single<size_t>(resp_data);
            return ret;
        }

        /** @brief Return the number of entries in the attached DB */
        size_t db_size() {
            req_.send(DB_SIZE);
            ZMQMessage resp = req_.read();
            const char *resp_data = resp.data();
            size_t ret = unpack_single<size_t>(resp_data);
            return ret;
        }

        /** @brief Add an entry into the connected ParDB */
        void add_entry(DBEntry<>& e) {
            // Serialize the DB entry
            size_t msg_size = sizeof(msg_type_t)+e.serialize_size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, ADD_ENTRY);

            // Add the actual entry
            e.serialize(msg_ptr);

            // Send it to the ParDB
            req_.send(msg, msg_size);

            delete [] msg;

            req_.wait_ack();
        }

        string get_state() {
            req_.send(GET_STATE);
            ZMQMessage resp = req_.read();
            string resp_s {resp.data()};
            return resp_s;
        }

        /** @brief Add a filter directory to the par DB */
        void add_filter_dir(string dir) {
            // Serialize the filter directory and send it to the par DB
            size_t msg_size = dir.size() + sizeof(msg_type_t);
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, ADD_FILTER_DIR_BROADCAST);
            pack_string(msg_ptr, dir);

            // Send it to the ParDB
            req_.send(msg, msg_size);

            delete [] msg;

            req_.wait_ack();
        }

        /** @brief Install a filter in the par DB */
        void install_filter(string f) {
            size_t msg_size = f.size() + sizeof(msg_type_t);
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, INSTALL_FILTER_BROADCAST);

            strncpy(msg_ptr, f.c_str(), f.size());

            // Send it to the ParDB
            req_.send(msg, msg_size);

            delete [] msg;

            req_.wait_ack();
        }

        /** @brief Add entries from a properly formatted txt file*/
        void add_db_file(string f) {
            size_t msg_size = f.size() + sizeof(msg_type_t);
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, ADD_DB_FILE);

            strncpy(msg_ptr, f.c_str(), f.size());

            // Send it to the ParDB
            req_.send(msg, msg_size);

            delete [] msg;

            req_.wait_ack();
        }

        /** @brief Instruct the par DB to process filters */
        void process() {
            req_.send(PROCESS_BROADCAST);
            req_.wait_ack();
        }

        /** @brief Instruct the par DB to process filters */
        void clear_filters() {
            req_.send(CLEAR_FILTERS_BROADCAST);
            req_.wait_ack();
        }

        /** @brief Get an entry's value by key */
        string get_entry_by_key(dbkey_t k) {
            size_t msg_size = sizeof(msg_type_t) + sizeof(dbkey_t);
            char msg[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, GET_ENTRY_BY_KEY);
            pack_single(msg_ptr, k);

            req_.send(msg, msg_size);

            ZMQMessage resp =  req_.read();
            string resp_s {resp.data()};
            return resp_s;
        }

        /* @brief get a list of all filters installed in the DB */
        vector<string> installed_filters() {
            size_t msg_size = sizeof(msg_type_t);
            char msg[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, INSTALLED_FILTERS);

            req_.send(msg, msg_size);

            ZMQMessage resp =  req_.read();
            string resp_s {resp.data()};
            vector<string> ret;

            // boost gets confused by splitting an empty string
            if (resp_s.length() == 0) {
                return ret;
            }

            boost::split(ret, resp_s, boost::is_any_of(","));
            return ret;
        }

        vector<DBEntry<>> get_entries() {
            size_t msg_size = sizeof(msg_type_t);
            char msg[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, GET_ENTRIES);

            req_.send(msg, msg_size);

            ZMQMessage resp =  req_.read();
            vector<DBEntry<>> entries = SeqDB::deserialize_entries(resp.data());
            return entries;
        }

        vector<DBEntry<>> query(vector<string> tags) {
            size_t msg_size = sizeof(msg_type_t) + sizeof(size_t);
            for (auto & tag : tags) msg_size += (tag.size()+1); // add 1 extra for null terminator
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, QUERY);
            pack_single(msg_ptr, tags.size());
            for (auto & tag : tags) pack_string_null_term(msg_ptr, tag);

            req_.send(msg, msg_size);

            delete [] msg;

            ZMQMessage resp =  req_.read();
            vector<DBEntry<>> entries = SeqDB::deserialize_entries(resp.data());
            return entries;

        }

        vector<DBEntry<>> query(string tag) {
            vector<string> tags {tag};
            return query(tags);
        }

        vector<string> neighbors() {
            size_t msg_size = sizeof(msg_type_t);
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, GET_NEIGHBORS);

            req_.send(msg, msg_size);

            delete [] msg;

            ZMQMessage resp =  req_.read();
            vector<string> addrs;
            const char *resp_data = resp.data();
            size_t num_neighbors = unpack_single<size_t>(resp_data);
            for (size_t i = 0; i < num_neighbors; i++) {
                uint64_t ser_addr = unpack_single<uint64_t>(resp_data);
                ZMQAddress addr {ser_addr};
                addrs.push_back(addr.get_addr_str());
            }
            return addrs;
        }

        bool processing() {
            req_.send(PROCESSING);
            ZMQMessage resp = req_.read();
            const char *resp_data = resp.data();
            size_t ret = unpack_single<bool>(resp_data);
            return ret;
        }

        void import_db(string dir) {
            size_t msg_size = dir.size() + sizeof(msg_type_t);
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, IMPORT_DB_DISTRIBUTE);

            strncpy(msg_ptr, dir.c_str(), dir.size());

            // Send it to the ParDB
            req_.send(msg, msg_size);

            delete [] msg;

            req_.wait_ack();
        }

        void export_db(string dir) {
            size_t msg_size = dir.size() + sizeof(msg_type_t);
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, EXPORT_DB_BROADCAST);

            strncpy(msg_ptr, dir.c_str(), dir.size());

            // Send it to the ParDB
            req_.send(msg, msg_size);

            delete [] msg;

            req_.wait_ack();
        }

        void export_db_with_tag(string dir, string tag) {
            char sep = '|';
            if (dir.find(sep) != string::npos || tag.find(sep) != string::npos) {
                throw runtime_error("The character '|' is not allowed in export directory or tag");
            }

            string data = dir + sep + tag;

            size_t msg_size = data.size() + sizeof(msg_type_t);
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, EXPORT_DB_WITH_TAG_BROADCAST);

            strncpy(msg_ptr, data.c_str(), data.size());

            // Send it to the ParDB
            req_.send(msg, msg_size);

            delete [] msg;

            req_.wait_ack();
        }

        /** @brief Instruct the par DB to stage_close (DEBUGGING) */
        void stage_close() {
            req_.send(STAGE_CLOSE);
            req_.wait_ack();
        }
        /** @brief Instruct the par DB to check_at_barrier (DEBUGGING) */
        void check_at_barrier() {
            req_.send(CHECK_AT_BARRIER);
            req_.wait_ack();
        }

        /** Tell the DB to print its entries (DEBUGGING) */
        void print_entries() {
            req_.send(PRINT_ENTRIES);
            req_.wait_ack();
        }
};

}
