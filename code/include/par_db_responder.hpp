#pragma once

#include "par_db_participant.hpp"

#include "pack.hpp"
#include "chatterbox.hpp"
#include "random_key_gen.hpp"

using namespace std;
using namespace elga;

namespace pando {

/** @brief Contains a parallel database proxy
 *
 * This does not participate as an agent, but can proxy into the mesh
 **/
class PandoResponder : public PandoParticipant {
    private:
        RandomKeyGen random_key_gen_;
        PandoMapClient db_;
    public:
        /** @brief Initialize the proxy without joining as an agent */
        PandoResponder(ZMQAddress addr) : 
                PandoParticipant(addr, false),
                random_key_gen_(addr_ser_), 
                db_(addr+MAP_LOCALNUM_OFFSET-RESPONDER_LOCALNUM_OFFSET, addr) {}

        PandoResponder(ZMQAddress addr, size_t) : PandoResponder(addr) {}

        /** @brief Handle specific, custom messages */
        virtual void process_msg(msg_type_t type, zmq_socket_t sock, const char *data, [[maybe_unused]] const char* end) {
            if (type == GET_ENTRY_BY_KEY)
                recv_get_entry_by_key(sock, data, end);
            else if (type == GET_ENTRIES_BY_KEYS)
                recv_get_entries_by_keys(sock, data, end);
            else recv_unknown_msg();
        }

        /** @brief Look up a batch of keys owned by this agent
         *
         * Request: <size_t n> <n x dbkey_t>. The reply is the PandoMap's
         * MAP_RETRIEVE_VALUES reply, forwarded as is: the received ZeroMQ
         * message is sent on the requester's socket without being copied.
         */
        void recv_get_entries_by_keys(zmq_socket_t sock, const char* data, const char* end) {
            size_t n;
            unpack_single(data, n);
            if ((size_t)(end - data) != n*sizeof(dbkey_t))
                throw runtime_error("GET_ENTRIES_BY_KEYS: request size does not match key count");

            ZMQMessage resp = db_.retrieve_values_raw(n, (const dbkey_t*)data);
            resp.send(sock);
        }

        void recv_get_entry_by_key(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            dbkey_t key;
            unpack_single(data, key);

            string value = "";
            if (db_.key_exist(key)) {
                auto entry = db_.retrieve(key);
                value = entry.value();
            }

            size_t msg_size = value.size(); //add 1 for null terminator
            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            pack_string(msg_ptr, value);

            send(sock, msg, msg_size);

            delete [] msg;
        }
};

}
