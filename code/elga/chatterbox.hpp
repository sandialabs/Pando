/**
 * ElGA chatterbox and networking
 *
 * Author: Kasimir Gabert
 *
 * Copyright 2021 National Technology & Engineering Solutions of Sandia, LLC
 * (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 * Government retains certain rights in this software.
 *
 * Please see the LICENSE.md file for license information.
 */

#ifndef CHATTERBOX_HPP
#define CHATTERBOX_HPP

#include "types.hpp"
#include "timer.hpp"
#include "address.hpp"

#include <string>
#include <vector>
#include <tuple>

#include <zmq.h>

namespace elga {

    using zmq_socket_t = void*;

    /** Helper function to build a socket */
    zmq_socket_t socket_(int type, uint64_t affinity=0x0, bool use_buffering=true);

    /** Helper function to bind to an address */
    void bind_(zmq_socket_t socket, const char *addr);

    /** Handles receiving and replying to a message, as appropriate */
    class ZMQMessage {
        private:
            zmq_msg_t msg_part_;
            zmq_socket_t sock_;
            /** Size in bytes. size_t, not int: zmq_msg_recv/zmq_msg_send
             *  return an int clamped to INT_MAX, so sizes are taken from
             *  zmq_msg_size() instead */
            size_t size_;
            char *data_;
            bool closed_;

            /** Take over that's message, leaving that empty (used by moves) */
            void take_(ZMQMessage& that);

        public:
            /** Read a message from a socket */
            ZMQMessage(zmq_socket_t sock, bool block=true);
            /** Prepare a message for writing */
            ZMQMessage(zmq_socket_t sock, size_t size);
            /** Release the message */
            ~ZMQMessage();

            /** Disallow copies */
            ZMQMessage(const ZMQMessage&) = delete;
            ZMQMessage& operator=(const ZMQMessage&) = delete;

            /** Handle moves by preventing the old from closing */
            ZMQMessage(ZMQMessage&& that);
            ZMQMessage& operator=(ZMQMessage&& that);

            /** Get the message size */
            size_t size() const { return size_; }

            /** Access the message data */
            const char *data() const { return data_; }

            /** Return the end of the data (1 passed the last valid byte) */
            const char *end() const { return data_+size_; }

            /** Access and change message data */
            char *edit_data() { return data_; }

            /** Send the message on the socket it was created for */
            void send();
            /** Send the message on another socket, e.g. to forward a message
             *  received on one socket without copying it. ZeroMQ takes over
             *  the buffer, so this message is empty (and closed) afterwards;
             *  sending it again throws. */
            void send(zmq_socket_t sock);
    };

    /** Handles requests to servers */
    class ZMQRequester {
        private:
        protected:
            ZMQAddress server_;
            zmq_socket_t sock_;
        public:
            ZMQRequester(const ZMQAddress server, const ZMQAddress &myself, addr_type_t at=REQUEST, bool use_buffering=true);
            ZMQRequester();
            ~ZMQRequester();

            /** Disallow copies */
            ZMQRequester(const ZMQRequester&) = delete;
            ZMQRequester& operator=(const ZMQRequester&) = delete;

            /** Disallow moves */
            ZMQRequester(ZMQRequester&& that) = delete;
            ZMQRequester& operator=(ZMQRequester&& that) = delete;

            /** Support swaps */
            void swap(ZMQRequester &that) {
                server_.swap(that.server_);
                std::swap(sock_, that.sock_);
            };

            /** Send a message to the server */
            void send(const char *data, size_t size, bool nowait=false);
            /** Send a simple message to the server */
            void send(msg_type_t type);
            /** Wait for an ack */
            void wait_ack();
            /** Read the server response */
            ZMQMessage read();

            /** Get a message to send */
            ZMQMessage prepare_send(size_t size);

            /** Retrieve the server address */
            uint64_t addr() { return server_.serialize(); }

            static int get_sock_type(zmq_socket_t s) {
                int socktype;
                size_t sock_len = sizeof(socktype);

                if (zmq_getsockopt(s, ZMQ_TYPE, &socktype, &sock_len) != 0)
                    throw std::runtime_error("Unable to get socket type");
                if (sock_len != sizeof(socktype))
                    throw std::runtime_error("Unable to get socket type, size issue");

                return socktype;
            }

            static bool is_reqrep_sock(zmq_socket_t s) {
                auto st = get_sock_type(s);
                return (st == ZMQ_REQ || st == ZMQ_REP);
            }

            int get_sock_type() {
                return get_sock_type(sock_);
            }
    };

    /**
     * The basic chatterbox implementation for ZMQ based
     * agents/streamers/clients/directories/etc.
     * This class handles the various connections, both establishing them,
     * waiting for initialization, and handling receiving/sending.
     */
    class ZMQChatterbox {
        private:
            // Hold our sockets
            // Note: requests are handled independently

            /** Socket for requests to us */
            zmq_socket_t sock_rep_;
            /** Socket for our publications */
            zmq_socket_t sock_pub_;
            /** Socket for our subscriptions */
            zmq_socket_t sock_sub_;
            /** Socket for our pulls */
            zmq_socket_t sock_pull_;

            /** Keep track of the time since the last heartbeat */
            timer::TimePoint last_heartbeat_;

        protected:
            /** Keep track of our address */
            const ZMQAddress addr_;

            const int64_t heartbeat_us = 1000000;       // 1 second

        public:
            /** Initialize the ZMQ context */
            static void Setup(int num_threads=1);
            /** Teardown the global ZMQ context */
            static void Teardown();

            /** Setup the chatterbox at the given address */
            ZMQChatterbox(const ZMQAddress &addr);
            virtual ~ZMQChatterbox();

            // Prevent move/copy
            ZMQChatterbox(const ZMQChatterbox&) = delete;
            ZMQChatterbox(ZMQChatterbox&&) = delete;
            ZMQChatterbox& operator=(const ZMQChatterbox&) = delete;
            ZMQChatterbox& operator=(ZMQChatterbox&&) = delete;

            /** Indefinitely poll for any request */
            std::vector<zmq_socket_t> poll(long timeout=2500);

            /** Send out to the given socket */
            static void send(zmq_socket_t sock, const char *data, size_t size, bool nowait=false);

            /** Send out an ack to a request */
            static void ack(zmq_socket_t sock);

            /** Publish a message */
            void pub(const char* data, size_t size);

            /** Begin a publish message */
            ZMQMessage prepare_pub(size_t size);

            /** Connect to a publisher */
            void sub_connect(const ZMQAddress &addr);

            /** Disconnect from a publisher */
            void sub_disconnect(const ZMQAddress &addr);

            /** Subscribe to a feed */
            void sub(msg_type_t type);

            /** Subscribe to a variable sized feed */
            void sub(const char *data, size_t size);

            /** Unsubscribe from a feed */
            void unsub(const char *data, size_t size);

            /** Determine if a heartbeat should be sent */
            bool should_send_heartbeat() {
                if (last_heartbeat_.distance_us() < heartbeat_us)
                    return false;
                last_heartbeat_ = timer::TimePoint();
                return true;
            }
    };

}

#endif
