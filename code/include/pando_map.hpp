#pragma once

#include "absl/container/flat_hash_map.h"
#include "dbentry.hpp"
#include "address.hpp"
#include "par_db_participant.hpp"
#include "pack.hpp"
#include <thread>
#include <arpa/inet.h>
#include <sstream>
#include "big_space.hpp"


using namespace std;
using namespace elga;

namespace pando {

class PandoMap : public PandoParticipant {
    public:
        using Space = BigSpace;
        using Alloc = BigSpaceAllocator<char>;
    private:
        Alloc alloc_;
        absl::flat_hash_map<dbkey_t, DBEntry<Alloc>> map_;
        ZMQAddress addr_;

        /** Socket for our pando map requests*/
        zmq_socket_t sock_map_;

    public:

        PandoMap(const ZMQAddress &addr, size_t space_size) : PandoParticipant(addr, false), alloc_(make_shared<Space>(space_size)) {
            map_.reserve(1<<22);
        }
        PandoMap(const ZMQAddress &addr) : PandoMap(addr, (size_t)(2ull*(1ull<<29)) ) { }

        bool does_key_exist(dbkey_t key) {
            auto it = map_.find(key);
            return !(it == map_.end());
        }

        DBEntry<Alloc>* retrieve(dbkey_t key) {
            auto it = map_.find(key);
            if (it == map_.end()) {
                throw runtime_error("Unable to find entry to retrieve");
            }
            DBEntry<Alloc>* entry = &(it->second);
            entry->set_key(key);

            return entry;
        }

        void insert(DBEntry<Alloc> entry) {
            map_.insert_or_assign(entry.get_key(), move(entry));
        }

        void recv_map_insert(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            insert({alloc_, data});

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ZMQChatterbox::ack(sock);

        }

        void recv_map_insert_multiple(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            while (data < end) {
                insert({alloc_, data});
            }

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ZMQChatterbox::ack(sock);

        }

        void recv_map_retrieve(zmq_socket_t sock, const char* data, [[maybe_unused]] const char* end) {
            dbkey_t key;
            unpack_single(data, key);

            DBEntry<Alloc>* entry = retrieve(key);

            size_t ser_size = entry->serialize_size();
            size_t msg_size = ser_size;
            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            entry->serialize(msg_ptr);

            ZMQChatterbox::send(sock, msg, msg_size);

            delete [] msg;
        }

        void recv_map_retrieve_if_exists(zmq_socket_t sock, const char* data, [[maybe_unused]] const char* end) {
            dbkey_t key;
            unpack_single(data, key);

            DBEntry<Alloc>* entry = nullptr;
            bool exists = true;
            size_t ser_size = 0;

            try {
                entry = retrieve(key);
                ser_size = entry->serialize_size();
            } catch (...) {
                exists = false;
            }

            size_t msg_size = sizeof(bool)+ser_size;
            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            pack_single(msg_ptr, exists);
            if (exists)
                entry->serialize(msg_ptr);

            ZMQChatterbox::send(sock, msg, msg_size);

            delete [] msg;
        }

        void recv_map_size(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            size_t res = map_.size();

            size_t msg_size = sizeof(size_t);
            char msg[msg_size];
            char *msg_ptr = msg;
            pack_single(msg_ptr, res);

            ZMQChatterbox::send(sock, msg, msg_size);
        }

        void recv_map_get_keys(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            size_t msg_size = sizeof(size_t) + (sizeof(dbkey_t) * map_.size());
            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            pack_single(msg_ptr, map_.size());

            for (auto & [key, entry] : map_) {
                pack_single(msg_ptr, key);
            }

            ZMQChatterbox::send(sock, msg, msg_size);

            delete [] msg;
        }

        void recv_map_get_entries(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            size_t msg_size = sizeof(size_t);
            // Iterate through the DB, finding the serialize size everywhere
            for (auto & [key, entry] : map_) {
                msg_size += entry.serialize_size();
            }

            // Allocate the msg
            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            // First, pack the size of the message into the message itself.
            // This *should* be handled by ZeroMQ, but since we're sending huge
            // messages, the message size can exceed the MAX_INT value, and
            // then the size is incorrectly determined. So let's do that
            // ourselves
            pack_single(msg_ptr, msg_size);

            // Serialize all entries into it
            for (auto & [key, entry] : map_) {
                entry.serialize(msg_ptr);
            }

            ZMQChatterbox::send(sock, msg, msg_size);

            delete [] msg;
        }

        /** @brief Return the keys and tags (but not values) of the requested entries
         *
         * Request: <num keys> <key>...
         * Response: <msg size> <entry serialized with an empty value>...
         * Keys that do not exist are skipped.
         */
        void recv_map_get_tags_by_keys(zmq_socket_t sock, const char* data, [[maybe_unused]] const char* end) {
            size_t num_keys;
            unpack_single(data, num_keys);

            vector<const DBEntry<Alloc>*> found;
            found.reserve(num_keys);
            size_t msg_size = sizeof(size_t);
            for (size_t i = 0; i < num_keys; ++i) {
                dbkey_t key;
                unpack_single(data, key);
                auto it = map_.find(key);
                if (it == map_.end()) continue;
                it->second.set_key(key);
                found.push_back(&(it->second));
                msg_size += it->second.serialize_size(false);
            }

            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            // Pack the size ourselves, as in recv_map_get_entries
            pack_single(msg_ptr, msg_size);

            for (auto entry : found)
                entry->serialize(msg_ptr, false);

            ZMQChatterbox::send(sock, msg, msg_size);

            delete [] msg;
        }

        /** @brief Look up whole entries (keys, tags and values) for a run of keys
         *
         * Request: [size_t count][size_t max_bytes][count x dbkey_t]
         * Reply:   [size_t keys_covered][entry]...
         *
         * The keys are looked up in order. Keys that do not exist are skipped,
         * and the entries that do exist are serialized in key order. Once the
         * reply would grow past max_bytes, no more keys are looked up, so a
         * reply never holds much more than max_bytes (it always covers at
         * least one key, however large its entry). keys_covered says how many
         * of the requested keys the reply accounts for; the caller asks again
         * for the rest. A malformed request is answered with keys_covered = 0.
         */
        void recv_map_get_entries_by_keys(zmq_socket_t sock, const char* data, const char* end) {
            size_t num_keys = 0, max_bytes = 0;
            bool ok = (size_t)(end - data) >= 2*sizeof(size_t);
            if (ok) {
                unpack_single(data, num_keys);
                unpack_single(data, max_bytes);
                ok = num_keys <= (size_t)(end - data) / sizeof(dbkey_t) &&
                     (size_t)(end - data) == num_keys * sizeof(dbkey_t);
            }
            if (!ok) num_keys = 0;

            vector<const DBEntry<Alloc>*> found;
            size_t msg_size = sizeof(size_t);
            size_t covered = 0;
            for (; covered < num_keys; ++covered) {
                if (covered > 0 && msg_size >= max_bytes) break;
                dbkey_t key;
                unpack_single(data, key);
                auto it = map_.find(key);
                if (it == map_.end()) continue;
                it->second.set_key(key);
                found.push_back(&(it->second));
                msg_size += it->second.serialize_size();
            }

            char* msg = new char[msg_size];
            char *msg_ptr = msg;
            pack_single(msg_ptr, covered);
            for (auto entry : found)
                entry->serialize(msg_ptr);

            ZMQChatterbox::send(sock, msg, msg_size);

            delete [] msg;
        }

        void recv_map_does_key_exist(zmq_socket_t sock, const char* data, [[maybe_unused]] const char* end) {
            dbkey_t key;
            unpack_single(data, key);
            bool exist = does_key_exist(key);

            size_t msg_size = sizeof(bool);
            char msg[msg_size];
            char *msg_ptr = msg;
            pack_single(msg_ptr, exist);

            ZMQChatterbox::send(sock, msg, msg_size);
        }

        /** @brief Look up the values for a batch of keys
         *
         * Request: <size_t n> <n x dbkey_t>
         * Reply:   <size_t n> then for each key, in order:
         *          <bool exists> <size_t len> <len bytes of value>
         *          (len is 0 and no bytes follow if the key does not exist)
         * Only values are returned, not tags.
         */
        void recv_map_retrieve_values(zmq_socket_t sock, const char* data, const char* end) {
            // A malformed request (too short, or a key count that does not
            // match its size) gets a reply with no values. The caller's parser
            // rejects that (wrong number of values), so it gets an error
            // instead of waiting forever, and nothing past the message is read.
            size_t n = 0;
            bool valid = (size_t)(end - data) >= sizeof(size_t);
            if (valid) {
                unpack_single(data, n);
                valid = (size_t)(end - data) == n*sizeof(dbkey_t);
            }
            if (!valid) {
                size_t zero = 0;
                ZMQChatterbox::send(sock, (const char*)&zero, sizeof(zero));
                return;
            }
            const char* keys_data = data;

            // Find each entry once and size the reply
            vector<DBEntry<Alloc>*> found(n, nullptr);
            size_t msg_size = sizeof(size_t);
            for (size_t i = 0; i < n; ++i) {
                dbkey_t key;
                unpack_single(keys_data, key);
                auto it = map_.find(key);
                if (it != map_.end()) found[i] = &(it->second);
                msg_size += sizeof(bool) + sizeof(size_t) + (found[i] ? found[i]->value().size() : 0);
            }

            // Build the reply directly in a ZeroMQ message, so the values are
            // copied once (from the map into the message) rather than into a
            // buffer and then again into a message by zmq_send
            ZMQMessage out {sock, msg_size};
            char* msg_ptr = out.edit_data();
            pack_single(msg_ptr, n);
            for (size_t i = 0; i < n; ++i) {
                bool exists = found[i] != nullptr;
                size_t len = exists ? found[i]->value().size() : 0;
                pack_single(msg_ptr, exists);
                pack_single(msg_ptr, len);
                if (len) {
                    memcpy(msg_ptr, found[i]->value().data(), len);
                    msg_ptr += len;
                }
            }

            out.send();
        }

        void process_msg(msg_type_t type, zmq_socket_t sock, const char *data, const char* end) {
            if (type == MAP_INSERT) 
                recv_map_insert(sock, data, end);
            else if (type == MAP_INSERT_MULTIPLE)
                recv_map_insert_multiple(sock, data, end);
            else if (type == MAP_RETRIEVE)
                recv_map_retrieve(sock, data, end);
            else if (type == MAP_RETRIEVE_IF_EXISTS)
                recv_map_retrieve_if_exists(sock, data, end);
            else if (type == MAP_SIZE)
                recv_map_size(sock, data, end);
            else if (type == MAP_GET_KEYS)
                recv_map_get_keys(sock, data, end);
            else if (type == MAP_GET_ENTRIES)
                recv_map_get_entries(sock, data, end);
            else if (type == MAP_GET_TAGS_BY_KEYS)
                recv_map_get_tags_by_keys(sock, data, end);
            else if (type == MAP_GET_ENTRIES_BY_KEYS)
                recv_map_get_entries_by_keys(sock, data, end);
            else if (type == MAP_DOES_KEY_EXIST)
                recv_map_does_key_exist(sock, data, end);
            else if (type == MAP_RETRIEVE_VALUES)
                recv_map_retrieve_values(sock, data, end);
            else
                throw runtime_error("Unknown message type");
        }

        /** @brief Return the SeqDB allocator used for large data */
        Alloc& get_allocator() {
            return alloc_;
        }

        DBEntry<Alloc> realloc_entry(DBEntry<> entry) {
            DBEntry<Alloc> new_entry {alloc_};
            new_entry.value().assign(entry.value());
            new_entry.set_key(entry.get_key());
            for (auto& tag : entry.tags()) {
                new_entry.add_tag(tag);
            }

            return new_entry;
        }

        DBEntry<> realloc_entry(DBEntry<Alloc> entry) {
            DBEntry<> new_entry;
            new_entry.value().assign(entry.value());
            new_entry.set_key(entry.get_key());
            for (auto& tag : entry.tags()) {
                new_entry.add_tag(tag);
            }

            return new_entry;
        }

        //DEBUGGING
        void print_entries() {
            for (auto & [key, entry] : map_) {
                cout << entry << "\n---------" << endl;
            }
        }
};

}
