#pragma once

#include "absl/container/flat_hash_map.h"

#include "pack.hpp"
#include "consistenthasher.hpp"
#include "chatterbox.hpp"
#include "dbentry.hpp"

using namespace std;
using namespace elga;

namespace pando {

/** @brief A Pando Participant knows how to distribute data across the DB */
class PandoParticipant : public ZMQChatterbox {
    private:
        /** @brief Keep a list of current neighbors */
        typedef list<ZMQRequester> l_req;
        l_req neighbors_;
        l_req responders_;
        absl::flat_hash_map<addr_t, l_req::iterator> l_lookup_;
        absl::flat_hash_map<addr_t, l_req::iterator> responder_lookup_;

        vector<addr_t> agents_;
        NoReplication rm_;

        void recv_num_neighbors(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            size_t res = num_neighbors();

            size_t msg_size = sizeof(size_t);
            char msg[msg_size];
            char *msg_ptr = msg;
            pack_single(msg_ptr, res);

            send(sock, msg, msg_size);
        }

        void recv_add_neighbor(zmq_socket_t sock, const char* data, [[maybe_unused]] const char* end) {
            // Get the serialized peer address
            addr_t peer;
            unpack_single(data, peer);

            bool notify = is_agent_;
            add_neighbor(peer, notify);
            if (!notify) {
                // Request a mesh update
                request_mesh(peer);
            }

            ack(sock);
        }

        /** @brief Get a list of Pando neighbors */
        void recv_get_neighbors(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            size_t num_neighbors = this->num_neighbors();

            size_t msg_size = sizeof(size_t) + (sizeof(uint64_t) * num_neighbors);
            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            pack_single(msg_ptr, num_neighbors);
            for (auto & requester : neighbors_) {
                pack_single(msg_ptr, requester.addr());
            }

            send(sock, msg, msg_size);

            delete [] msg;
            
        }


        void send_handshake(ZMQRequester& req) {
            // Build the message
            // Message: HANDSHAKE | mesh size | mesh addresses
            size_t data_size = sizeof(msg_type_t)+sizeof(size_t)+agents_.size()*sizeof(addr_t);
            char* data = new char[data_size];
            char* data_ptr = data;

            pack_msg(data_ptr, HANDSHAKE);
            pack_single(data_ptr, agents_.size());
            for (addr_t agent : agents_)
                pack_single(data_ptr, agent);

            req.send(data, data_size);

            delete [] data;
        }

        void process_new_mesh(const char* data) {
            // Get the serialized mesh and merge it with ours
            size_t num_elements;
            unpack_single(data, num_elements);

            for (; num_elements > 0; --num_elements) {
                addr_t peer;
                unpack_single(data, peer);
                add_neighbor(peer, false);
            }
        }

        void recv_handshake([[maybe_unused]] zmq_socket_t sock, const char* data, [[maybe_unused]] const char* end) {
            process_new_mesh(data);
        }

        virtual void custom_heartbeat() { }

        void heartbeat() {
            custom_heartbeat();

            // Only send if there's a need, e.g., mesh has changed
            if (hb_ctr_ == 0) return;
            --hb_ctr_;

            // Publish our mesh
            size_t pub_data_size = sizeof(msg_type_t)+sizeof(size_t)+agents_.size()*sizeof(addr_t);
            char* pub_data = new char[pub_data_size];
            char* pub_data_ptr = pub_data;

            pack_msg(pub_data_ptr, HEARTBEAT);
            pack_single(pub_data_ptr, agents_.size());
            for (addr_t agent : agents_) {
                pack_single(pub_data_ptr, agent);
            }

            pub(pub_data, pub_data_size);

            delete [] pub_data;
        }

        void recv_ring_size(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            size_t res = ch_.ring_size();

            size_t msg_size = sizeof(size_t);
            char msg[msg_size];
            char *msg_ptr = msg;
            pack_single(msg_ptr, res);

            send(sock, msg, msg_size);
        }

        void recv_heartbeat([[maybe_unused]] zmq_socket_t sock, const char* data, [[maybe_unused]] const char* end) {
            process_new_mesh(data);
        }

        void recv_want_heartbeat([[maybe_unused]] zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            hb_ctr_ = hb_ctr_max;
        }

    protected:
        /** @brief Keep track of our own address */
        ZMQAddress addr_;
        addr_t addr_ser_;

        /** @brief Store the consistent hasher */
        ConsistentHasher ch_;

        /** @brief Update the consistent hash ring with the current agents */
        void update_ch() {
            // Use STARTING_VAGENTS for each agent
            vector<addr_t> virtual_agents;
            virtual_agents.reserve(agents_.size()*STARTING_VAGENTS);
            for (addr_t agent : agents_) {
                for (aid_t a = 0; a < STARTING_VAGENTS; ++a) {
                    virtual_agents.push_back(pack_agent(agent, a));
                }
            }
            ch_.update_agents(virtual_agents);
        }

        /** @brief Store if we are participating as an agent */
        bool is_agent_;

        const static size_t hb_ctr_max = 3;
        size_t hb_ctr_;

        l_req::iterator get_req(addr_t addr, bool responder=false) {
            if (!responder)
                return l_lookup_[addr];
            else
                return responder_lookup_[addr];
        }

    public:
        /** @brief Initial the participant at the given address
         *
         * @param addr the address to listen on
         * @param is_agent if true, participate as an agent (join the ring for storage, etc.)
         */
        PandoParticipant(ZMQAddress addr, bool is_agent) : ZMQChatterbox(addr), addr_(addr), addr_ser_(addr_.serialize()), ch_(agents_, rm_), is_agent_(is_agent), hb_ctr_(0) {
            if (is_agent_)
                add_neighbor(addr.serialize(), false);
            // Subscribe to any appropriate messages
            sub(HEARTBEAT);
        }

        void recv_unknown_msg() {
            throw runtime_error("Unknown message type received");
        }

        /** @brief Receive specific messages for the participant */
        virtual void process_msg([[maybe_unused]] msg_type_t type, [[maybe_unused]] zmq_socket_t sock, [[maybe_unused]] const char *data, [[maybe_unused]] const char *end) {
            recv_unknown_msg();
        }

        /** @brief Determine if a dbkey is owned by the participant */
        bool has_ownership(dbkey_t key) {
            return (ch_.lookup_agent(key) == addr_ser_);
        }

        addr_t lookup_agent(dbkey_t key) {
            return ch_.lookup_agent(key);
        }

        /** @brief Find the Requestor for a given database key */
        l_req::iterator find_req(dbkey_t key, bool responder=false) {
            addr_t agent = lookup_agent(key);
            if (!responder) {
                auto req = l_lookup_.find(agent);
                if (req == l_lookup_.end()) throw runtime_error("Invalid agent, cannot find requestor");
                return req->second;
            } else {
                //Check the cache for the requester to the responder, create it if necessary
                auto req = responder_lookup_.find(agent);
                if (req == responder_lookup_.end()) {
                    ZMQAddress resp_addr {agent};
                    resp_addr = resp_addr + RESPONDER_LOCALNUM_OFFSET;
                    responders_.emplace_back(resp_addr, addr_);
                    l_req::iterator new_resp_it = prev(responders_.end());
                    responder_lookup_[agent] = new_resp_it;
                    return new_resp_it;
                }
                return req->second;
            }
        }

        /** @brief Return the original, full message that has been passed to a
         * recv_* function */
        static const char* get_full_msg(const char* data) {
            return data-sizeof(msg_type_t);
        }

        /** @brief Recv zero to several messages and process them
         *
         * Note that this will receive at most one message per socket type.
         * The server by default runs with four socket types (e.g., requests,
         * subscriptions, and pulls) and so can receive at most four messages.
         *
         * This should be called in a loop continuously to process all messages
         * as they occur.
         *
         * This returns false if no messages were processed
         *
         * @param timeout_ms how long to wait for a message (0 to not wait)
         */
        bool recv_msg(long timeout_ms = 500) {
            auto socks = poll(timeout_ms);
            for (auto sock : socks) {
                // Get the message
                ZMQMessage msg(sock);

                // Act on the message
                const char *data = msg.data();
                const char *end = msg.end();
                msg_type_t type = unpack_msg(data);

                if (type == HANDSHAKE) 
                    recv_handshake(sock, data, end);
                else if (type == NUM_NEIGHBORS)
                    recv_num_neighbors(sock, data, end);
                else if (type == ADD_NEIGHBOR)
                    recv_add_neighbor(sock, data, end);
                else if (type == GET_NEIGHBORS)
                    recv_get_neighbors(sock, data, end);
                else if (type == RING_SIZE)
                    recv_ring_size(sock, data, end);
                else if (type == HEARTBEAT)
                    recv_heartbeat(sock, data, end);
                else if (type == WANT_HEARTBEAT)
                    recv_want_heartbeat(sock, data, end);
                else
                    process_msg(type, sock, data, end);
            }
            if (should_send_heartbeat()) heartbeat();
            return socks.size() > 0;
        }

        void send_heartbeat() {
            heartbeat();
        }

        /** @brief Add a neighbor
         *
         * This adds and listens to the neighbor.  If set to notify, then it
         * will send a HANDSHAKE packet to the neighbor, which will result in
         * the neighbor then adding this back.
         *
         * The resulting point-to-point connection will later merge the two
         * meshes fully as heartbeats propagate the newly connected topologies.
         * After a few heartbeat times, the system should eventually converge.
         *
         * @param a the address of the neighbor
         * @param notify_neighbor if true, send a message to the neighbor
         *
         * @return true if the neighbor was added, false if it already exists
         */
        bool add_neighbor(addr_t a, bool notify_neighbor=true) {
            // Check if the neighbor exists already
            if (l_lookup_.find(a) != l_lookup_.end()) return false;

            hb_ctr_ = hb_ctr_max;

            ZMQAddress neigh_addr { a };
            ZMQAddress resp_addr = neigh_addr+RESPONDER_LOCALNUM_OFFSET;

            // Subscribe to any messages from the neighbor
            if (a != addr_ser_)
                sub_connect(neigh_addr);

            neighbors_.emplace_back(neigh_addr, addr_, PULL);
            l_req::iterator new_neigh_it = prev(neighbors_.end());
            l_lookup_[a] = new_neigh_it;

            // We know the agent is new
            agents_.push_back(a);
            update_ch();

            if (notify_neighbor) {
                // Send a handshake message
                send_handshake(*new_neigh_it);
            }

            return true;
        }

        size_t num_neighbors() { return neighbors_.size(); }

        l_req& get_neighbor_list() {
            return neighbors_;
        }

        /** @brief Return the full list of the known agents */
        vector<addr_t>& agents() { return agents_; }

        /** @brief Request a heartbeat from a peer */
        void request_mesh(addr_t peer) {
            auto req = l_lookup_.find(peer);
            if (req == l_lookup_.end()) throw runtime_error("Invalid peer, cannot find requestor");
            auto& r = req->second;

            r->send(WANT_HEARTBEAT);
        }

};

}
