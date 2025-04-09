#pragma once

#include <list>
#include <vector>
#include <thread>

#include "terr.hpp"
#include "pack.hpp"
#include "chatterbox.hpp"
#include "consistenthasher.hpp"

#include "par_db_participant.hpp"
#include "par_db_client.hpp"
#include "par_db_proxy.hpp"
#include "par_db_access.hpp"
#include "par_db_thread.hpp"
#include "par_db_responder.hpp"

using namespace std;
using namespace elga;

namespace pando {

typedef enum state_t {
    PRELOAD,
    STAGE_BEGIN,
    STAGE_PROCESSED,
    STAGE_CLOSING,
    STAGE_ALG_PROCESS
} state_t;

void s_ref_add_entry(fn_ref, DBEntry<>);
void s_ref_add_tag_to_entry(fn_ref, dbkey_t, string);
void s_ref_remove_tag(fn_ref, dbkey_t, string);
void s_ref_update_entry_val(fn_ref, dbkey_t, string);
void s_ref_subscribe_to_entry(fn_ref, dbkey_t, dbkey_t, string);
void s_ref_get_entry_by_key(fn_ref, dbkey_t, char**);

/** @brief Contains the parallel (distributed) database
 *
 * This runs as an agent in a distributed system
 */
class ParDB : public PandoParticipant {
    private:
        constexpr static const dbkey_t sync_key {0,0,0};

        /** @brief Keep the database */
        fn_refs db_refs_;
        SeqDBParAccess db_;

        state_t state_;

        bool local_process_again_ = false;
        bool synchronizer_process_again_ = false;

        absl::flat_hash_map<addr_t, size_t> sent_distribution_;
        absl::flat_hash_map<addr_t, size_t> sync_sent_dist_;
        size_t sync_sent_dist_count_;
        size_t sync_recv_at_barrier_;
        size_t local_sent_dist_;
        size_t local_recv_dist_;

        unordered_map<itkey_t, size_t> alg_msgs_recvd_at_iter_;
        unordered_map<itkey_t, bool> alg_active_at_iter_;
        bool has_alg_processed_ = true;
        size_t alg_vertex_msgs_received_ = 0;

        bool waiting_for_barrier_ = false;

        ParDBThread<PandoResponder> responder_;

        bool skip_group_filters_ = false;

    public:
        /** @brief Initialize the parallel DB */
        ParDB(ZMQAddress addr, size_t sz, bool skip_group_filters=false) :
                PandoParticipant(addr, true),
                db_refs_(this,
                    s_ref_add_entry,
                    s_ref_add_tag_to_entry,
                    s_ref_remove_tag,
                    s_ref_update_entry_val,
                    s_ref_subscribe_to_entry,
                    s_ref_get_entry_by_key
                ),
                db_(db_refs_, addr_ser_, sz),
                state_(PRELOAD),
                sync_sent_dist_count_(0),
                sync_recv_at_barrier_(0),
                local_sent_dist_(0),
                local_recv_dist_(0),
                responder_(addr + RESPONDER_LOCALNUM_OFFSET),
                skip_group_filters_(skip_group_filters) {
            sub(PROCESS);
            sub(ALGPROCESS);
            sub(END_BARRIER);
            sub(ADD_FILTER_DIR);
            sub(INSTALL_FILTER);
            sub(CLEAR_FILTERS);
            sub(EXPORT_DB);

            if (skip_group_filters_) db_.disable_group_filters();
        }
        ParDB(ZMQAddress addr) : ParDB(addr, 2ull*(1ull<<29)) { }

        /** @brief Send statistics as a custom heartbeat */
        virtual void custom_heartbeat() {
            size_t msg_size = sizeof(msg_type_t)+sizeof(addr_t)+sizeof(size_t);
            char msg[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, STATS);
            pack_single(msg_ptr, addr_ser_);

            size_t stat_size = db_size();
            pack_single(msg_ptr, stat_size);

            pub(msg, msg_size);
        }

        /** @brief Handle specific, custom messages */
        virtual void process_msg(msg_type_t type, zmq_socket_t sock, const char *data, [[maybe_unused]] const char* end) {
            if (type == ADD_ENTRY)
                recv_add_entry(sock, data, end);
            else if (type == DB_SIZE)
                recv_db_size(sock, data, end);
            else if (type == ADD_FILTER_DIR)
                recv_add_filter_dir(sock, data, end);
            else if (type == ADD_FILTER_DIR_BROADCAST)
                recv_add_filter_dir_broadcast(sock, data, end);
            else if (type == ADD_TAG_TO_ENTRY)
                recv_add_tag_to_entry(sock, data, end);
            else if (type == REMOVE_TAG_FROM_ENTRY)
                recv_remove_tag_from_entry(sock, data, end);
            else if (type == UPDATE_ENTRY_VAL)
                recv_update_entry_val(sock, data, end);
            else if (type == SUBSCRIBE_TO_ENTRY)
                recv_subscribe_to_entry(sock, data, end);
            else if (type == INSTALL_FILTER)
                recv_install_filter(sock, data, end);
            else if (type == INSTALLED_FILTERS)
                recv_installed_filters(sock, data, end);
            else if (type == GET_ENTRIES)
                recv_get_entries(sock, data, end);
            else if (type == INSTALL_FILTER_BROADCAST)
                recv_install_filter_broadcast(sock, data, end);
            else if (type == ADD_DB_FILE)
                recv_add_db_file(sock, data, end);
            else if (type == PROCESS)
                recv_process(sock, data, end);
            else if (type == ALGPROCESS)
                recv_algprocess(sock, data, end);
            else if (type == PROCESS_BROADCAST)
                recv_process_broadcast(sock, data, end);
            else if (type == QUERY)
                recv_query(sock, data, end);
            else if (type == START_BARRIER_WAIT)
                recv_start_barrier_wait(sock, data, end);
            else if (type == BARRIER_MSG_DIST)
                recv_barrier_msg_dist(sock, data, end);
            else if (type == AT_BARRIER)
                recv_at_barrier(sock, data, end);
            else if (type == END_BARRIER)
                recv_end_barrier(sock, data, end);
            else if (type == PROCESSING)
                recv_processing(sock, data, end);
            else if (type == ALG_INTERNAL_COMPUTE)
                recv_alg_internal_compute(sock, data, end);
            else if (type == ALG_VERTICES)
                recv_alg_vertices(sock, data, end);
            else if (type == CLEAR_FILTERS_BROADCAST)
                recv_clear_filters_broadcast(sock, data, end);
            else if (type == CLEAR_FILTERS)
                recv_clear_filters(sock, data, end);
            else if (type == EXPORT_DB)
                recv_export_db(sock, data, end);
            else if (type == EXPORT_DB_BROADCAST)
                recv_export_db_broadcast(sock, data, end);
            else if (type == IMPORT_DB)
                recv_import_db(sock, data, end);
            else if (type == IMPORT_DB_DISTRIBUTE)
                recv_import_db_distribute(sock, data, end);
            else if (type == GET_STATE)
                recv_get_state(sock, data, end);
            else if (type == PRINT_ENTRIES)
                recv_print_entries(sock, data, end);
            else recv_unknown_msg();
        }

        /** @brief Returns the size of the sequential DB */
        size_t db_size() { return db_.size(); }

        /** @brief Adds an entry to the global DB */
        void add_entry(DBEntry<> entry) {
            //Before we forward the entry, make sure it no longer has the INITIAL_KEY
            db_.verify_entry_key(&entry);

            dbkey_t key = entry.get_key();

            size_t ser_size = entry.serialize_size();
            size_t msg_size = sizeof(msg_type_t)+ser_size;
            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            pack_msg(msg_ptr, ADD_ENTRY);
            entry.serialize(msg_ptr);

            // Get the requestor for the given DBKey
            auto req = find_req(key);

            req->send(msg, msg_size);

            delete [] msg;

            update_sent_dist(req->addr());
        }

        /** @brief Add a tag to an existing entry */
        void add_tag_to_entry(dbkey_t key, string tag) {
            // Serialize the key and tag
            size_t msg_size = sizeof(msg_type_t)+sizeof(dbkey_t)+tag.size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, ADD_TAG_TO_ENTRY);

            // Add the actual entry
            pack_single(msg_ptr, key);
            pack_string(msg_ptr, tag);

            // Send it to the ParDB
            auto req = find_req(key);
            req->send(msg, msg_size);

            delete [] msg;

            update_sent_dist(req->addr());
        }

        /** @brief Remove a tag from an entry */
        void remove_tag_from_entry(dbkey_t key, string tag) {
            // Serialize the key and tag
            size_t msg_size = sizeof(msg_type_t)+sizeof(dbkey_t)+tag.size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, REMOVE_TAG_FROM_ENTRY);

            // Add the actual entry
            pack_single(msg_ptr, key);
            pack_string(msg_ptr, tag);

            // Send it to the ParDB
            auto req = find_req(key);
            req->send(msg, msg_size);

            delete [] msg;

            update_sent_dist(req->addr());
        }

        /** @brief Update the value of an entry */
        void update_entry_val(dbkey_t key, string tag) {
            // Serialize the key and tag
            size_t msg_size = sizeof(msg_type_t)+sizeof(dbkey_t)+tag.size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, UPDATE_ENTRY_VAL);

            // Add the actual entry
            pack_single(msg_ptr, key);
            pack_string(msg_ptr, tag);

            // Send it to the ParDB
            auto req = find_req(key);
            req->send(msg, msg_size);

            delete [] msg;

            update_sent_dist(req->addr());
        }

        /** @brief Subscribe to an entry (wait for it to be created) */
        void subscribe_to_entry(dbkey_t my_key, dbkey_t wait_key, string inactive_tag) {
            // Serialize the key and tag
            size_t msg_size = sizeof(msg_type_t)+sizeof(dbkey_t)+sizeof(dbkey_t)+inactive_tag.size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, SUBSCRIBE_TO_ENTRY);

            // Add the actual entry
            pack_single(msg_ptr, my_key);
            pack_single(msg_ptr, wait_key);
            pack_string(msg_ptr, inactive_tag);

            // Send it to the ParDB
            auto req = find_req(wait_key);
            req->send(msg, msg_size);

            delete [] msg;

            update_sent_dist(req->addr());
        }


        /** @brief Lookup an entry by key */
        void get_entry_by_key(dbkey_t key, char** res) {
            // Serialize the key and tag
            size_t msg_size = sizeof(msg_type_t)+sizeof(dbkey_t);
            char msg[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, GET_ENTRY_BY_KEY);

            // Pack the key into the message
            pack_single(msg_ptr, key);

            // Send it to the ParDB
            auto req = find_req(key, true); //Get the other node's responder
            req->send(msg, msg_size);

            ZMQMessage resp = req->read();
            const char *data = resp.data();
            const char *end = resp.end();
            string value { data, end };

            if (value == "\0") {
                //entry was not found, we want the result to be a nullptr
                res = nullptr;
                return;
            }
            *res = (char*)malloc(sizeof(char)*value.size()+1); // add one for the null terminator
            if (*res == nullptr) throw runtime_error("Unable to allocate memory");
            strcpy(*res, value.c_str());
        }

        /** @brief Broadcast the message to add a filter directory to the internal DB */
        void recv_add_filter_dir_broadcast(zmq_socket_t sock, const char* data, const char* end) {
            string dir {data, end};

            //mimic sending the message to ourselves as well
            recv_add_filter_dir(NULL, data, end);

            size_t msg_size = dir.size() + sizeof(msg_type_t);
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, ADD_FILTER_DIR);
            pack_string(msg_ptr, dir);

            // Send it to the ParDB
            pub(msg, msg_size);

            delete [] msg;

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);

        }

        /** @brief Add a filter directory to the internal DB */
        void recv_add_filter_dir(zmq_socket_t sock, const char* data, const char* end) {
            string dir {data, end};

            add_filter_dir(dir);

            // If necessary, respond with an acknowledgement
            if (sock != NULL && ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Install a filter and tell all neighbors to do the same */
        void recv_install_filter_broadcast(zmq_socket_t sock, const char* data, const char* end) {
            string f {data, end};
            recv_install_filter(NULL, data, end);

            size_t msg_size = f.size() + sizeof(msg_type_t);
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, INSTALL_FILTER);

            strncpy(msg_ptr, f.c_str(), f.size());

            // Send it to all neighbors
            pub(msg, msg_size);

            delete [] msg;

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Install a filter in the internal DB */
        void recv_install_filter(zmq_socket_t sock, const char* data, const char* end) {
            string f {data, end};

            install_filter(f);

            // If necessary, respond with an acknowledgement
            if (sock != NULL && ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Return a list of all installed filters */
        void recv_installed_filters(zmq_socket_t sock, [[maybe_unused]]const char* data, [[maybe_unused]]const char* end) {
            auto filters = db_.installed_filters();

            string sep = ",";

            string msg_s = "";
            for (size_t i=0; i<filters.size(); i++) {
                string filter_name = filters[i];
                msg_s += filter_name;
                if (i != filters.size() - 1)
                    msg_s += sep;
            }

            size_t msg_size = sizeof(msg_type_t) + msg_s.size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_string_null_term(msg_ptr, msg_s);

            send(sock, msg, msg_size);

            delete [] msg;
        }

        /** @brief Install a filter and tell all neighbors to do the same */
        void recv_clear_filters_broadcast(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            msg_type_t msg = CLEAR_FILTERS;
            pub((char*)&msg, sizeof(msg));

            //also send to self
            auto req = get_req(addr_.serialize());
            req->send(CLEAR_FILTERS);

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Install a filter in the internal DB */
        void recv_clear_filters(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            clear_filters();

            // If necessary, respond with an acknowledgement
            if (sock != NULL && ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        void clear_filters() {
            db_.clear_filters();
        }

        /** @brief Return a list of all entries*/
        void recv_get_entries(zmq_socket_t sock, [[maybe_unused]]const char* data, [[maybe_unused]]const char* end) {
            auto entries = db_.entries();

            size_t msg_size = db_.serialize_size();
            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            db_.serialize_entries(msg_ptr);

            send(sock, msg, msg_size);

            delete [] msg;
        }

        /** @brief Load entries from a text file into the DB */
        void recv_add_db_file(zmq_socket_t sock, const char* data, const char* end) {
            string f {data, end};

            add_db_file(f);

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        void recv_export_db_broadcast(zmq_socket_t sock, const char* data, const char* end) {
            string dir {data, end};

            size_t msg_size = sizeof(msg_type_t) + dir.size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, EXPORT_DB);
            pack_string(msg_ptr, dir);

            pub(msg, msg_size);
            // Also, send to ourselves
            auto req = get_req(addr_.serialize());
            req->send(msg, msg_size);

            delete [] msg;

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        void recv_export_db(zmq_socket_t sock, const char* data, const char* end) {
            string dir {data, end};

            export_db(dir);

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        void export_db(string dir) {
            db_.export_db(dir + "/pando-export-" + addr_.get_addr_str());
        }

        ///////////////////////////////////////////////////////////
        void recv_export_db_with_tag_broadcast(zmq_socket_t sock, const char* data, const char* end) {
            string data_str {data, end};


            size_t msg_size = sizeof(msg_type_t) + data_str.size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, EXPORT_DB);
            pack_string(msg_ptr, data_str);

            pub(msg, msg_size);
            // Also, send to ourselves
            auto req = get_req(addr_.serialize());
            req->send(msg, msg_size);

            delete [] msg;

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        void recv_export_db_with_tag(zmq_socket_t sock, const char* data, const char* end) {
            string data_str {data, end};

            std::vector<std::string> data_vec;
            boost::split(data_vec, data_str, boost::is_any_of("|"));
            if (data_vec.size() != 2) {
                cerr << "Received bad data for export_db_with_tag" << endl;
                if (ZMQRequester::is_reqrep_sock(sock))
                    ack(sock);
                return;
            }

            string dir = data_vec[0];
            string tag = data_vec[1];

            export_db_with_tag(dir, tag);

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        void export_db_with_tag(string dir, string tag) {
            db_.export_db(dir + "/pando-export-" + addr_.get_addr_str(), tag);
        }
        ///////////////////////////////////////////////////////////

        void recv_import_db_distribute([[maybe_unused]] zmq_socket_t sock, const char* data, const char* end) {
            string dir {data, end};

            // get all the files in that directory to import
            auto & neighbors = get_neighbor_list();
            auto n_it = neighbors.begin();

            if (!std::filesystem::exists(dir)) {
                cerr << "[ERROR] directory " << dir << " did not exist" << endl;
            } else {
                for (const auto & entry : std::filesystem::directory_iterator(dir)) {
                    auto& n_req = *n_it;

                    string full_fn = entry.path().string();

                    size_t msg_size = sizeof(msg_type_t) + full_fn.size();
                    char* msg = new char[msg_size];
                    char* msg_ptr = msg;

                    pack_msg(msg_ptr, IMPORT_DB);
                    pack_string(msg_ptr, full_fn);

                    n_req.send(msg, msg_size);

                    delete [] msg;

                    // Move to the next neighbor
                    ++n_it;
                    if (n_it == neighbors.end())
                        n_it = neighbors.begin();
                }
            }

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);

        }

        void recv_import_db([[maybe_unused]] zmq_socket_t sock, const char* data, const char* end) {
            string fn {data, end};
            import_db(fn);
        }

        void import_db(string fn) {
            db_.import_db(fn);
        }

        /** @brief Begin waiting for a barrier */
        void start_barrier_wait() {
            // We are at the barrier, let the synchronizer know
            auto req = find_req(sync_key);
            // We want to send the distribution of messages we sent out
            size_t msg_size = sizeof(msg_type_t)+sizeof(bool)+(sizeof(addr_t)+sizeof(size_t))*sent_distribution_.size();
            char *msg = new char[msg_size];
            char *msg_ptr = msg;

            pack_msg(msg_ptr, START_BARRIER_WAIT);
            pack_single(msg_ptr, local_process_again_);
            for (auto & [addr, count] : sent_distribution_) {
                pack_single(msg_ptr, addr);
                pack_single(msg_ptr, count);
            }

            req->send(msg, msg_size);

            delete [] msg;
        }

        /** @brief Receive a barrier wait from some agent */
        void recv_start_barrier_wait([[maybe_unused]] zmq_socket_t sock, const char* data, const char* end) {
            // Update the sent distributions
            bool run_again;
            unpack_single(data, run_again);
            if (run_again)
                synchronizer_process_again_ = true;

            while (data < end) {
                addr_t addr;
                size_t count;
                unpack_single(data, addr);
                unpack_single(data, count);

                sync_sent_dist_[addr] += count;
            }
            
            #ifdef VERBOSE
            cerr << "sync_sent_dist_count_ = " << sync_sent_dist_count_ + 1 << ", num neighbors = " << num_neighbors() << endl;
            #endif
            if (++sync_sent_dist_count_ == num_neighbors()) {
                // We have received all sent distributions, and can distribute
                // them, beginning the next stage of the barrier
                sync_recv_at_barrier_ = 0;
                auto& cur_agents = agents();
                for (auto addr : cur_agents) {
                    size_t count = sync_sent_dist_[addr];

                    size_t msg_size = sizeof(msg_type_t)+sizeof(size_t);
                    char msg[msg_size];
                    char* msg_ptr = msg;

                    pack_msg(msg_ptr, BARRIER_MSG_DIST);
                    pack_single(msg_ptr, count);

                    auto req = get_req(addr);
                    req->send(msg, msg_size);
                }
                sync_sent_dist_count_ = 0;
                sync_sent_dist_.clear();
            }
        }

        void update_sent_dist(addr_t addr) {
            if (state_ == PRELOAD) return;

            ++sent_distribution_[addr];
            if (waiting_for_barrier_)
                check_at_barrier();
        }

        void update_recv_dist() {
            if (state_ == PRELOAD) return;
            ++local_recv_dist_;
            if (waiting_for_barrier_)
                check_at_barrier();
        }

        void check_at_barrier() {
            #ifdef VERBOSE
            cerr << "local_sent_dist_ = " << local_sent_dist_ << ", local_recv_dist_ = " << local_recv_dist_  << endl;
            #endif
            if (local_sent_dist_ == local_recv_dist_) {
                waiting_for_barrier_ = false;

                // Reset the local recv counter
                local_recv_dist_ = 0;

                // We have received all required messages, and need to sent
                // that we are at the barrier to the synchronizer
                auto req = find_req(sync_key);
                req->send(AT_BARRIER);
            } else {
                waiting_for_barrier_ = true;
            }
        }

        void recv_barrier_msg_dist([[maybe_unused]] zmq_socket_t sock, const char* data, [[maybe_unused]] const char* end) {
            // Unpack the message and load our message dists
            unpack_single(data, local_sent_dist_);

            check_at_barrier();
        }

        void recv_at_barrier([[maybe_unused]] zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            #ifdef VERBOSE
            cerr << "sync_recv_at_barrier_ = " << sync_recv_at_barrier_ + 1 << ", num_neighbors = " << num_neighbors() << endl;
            #endif
            if (++sync_recv_at_barrier_ == num_neighbors()) {
                // Everyone has gotten to the barrier
                // The barrier is now over
                msg_type_t msg = END_BARRIER;
                pub((char*)&msg, sizeof(msg));
                // Also, send to ourselves
                auto req = get_req(addr_.serialize());
                req->send(END_BARRIER);
            }
        }

        virtual void start_db_process() {
            // Now, actually process the DB
            #ifdef VERBOSE
            cerr << "about to process filters" << endl;
            #endif
            local_process_again_ = db_.process();
            #ifdef VERBOSE
            cerr << "finished process filters" << endl;
            #endif

            finish_db_process();
        }

        // fixme start_db_process and finish_db_process not actually setup yet
        void finish_db_process() {
            // Wait for the next barrier
            state_ = STAGE_PROCESSED;
            start_barrier_wait();
        }

        void end_barrier_stage_begin() {
            start_db_process();
        }
        void end_barrier_stage_processed() {
            state_ = STAGE_CLOSING;
            #ifdef VERBOSE
            cerr << "starting stage close" << endl;
            #endif
            db_.stage_close();
            #ifdef VERBOSE
            cerr << "finished stage close" << endl;
            #endif

            // Wait for the next barrier
            start_barrier_wait();
        }
        void end_barrier_stage_closing() {
            // Now all agents have closed their stages
            // We can start the loop again
            if (local_process_again_) {
                // this is mostly unnecessary, but it is nice to not set to
                // PRELOAD if we're going to process again so it's easier to
                // know that the entire mesh has not finished processing
                state_ = STAGE_BEGIN;
            } else
                state_ = PRELOAD;

            //if we are the synchronizer, and need to process again, let everyone know to do that
            if (has_ownership(sync_key)) {
                // if we are the sync agent and we get to closing, then we must have received sent distributions from everyone and confirmed that they match(?) recv distributions
                if (synchronizer_process_again_) {
                    synchronizer_process_again_ = false;
                    has_alg_processed_ = false;
                    broadcast_process();
                } else {
                    // move everyone into the alg db process
                    if (skip_group_filters_) has_alg_processed_ = true;
                    if (!has_alg_processed_) {
                        has_alg_processed_ = true;
                        #ifdef VERBOSE
                        cerr << "about to broadcast an algprocess message" << endl;
                        #endif
                        broadcast_algprocess();
                    }
                }
            }
        }
        void alg_main_loop() {
            bool active = db_.compute_internal();

            // Read out db_.graph_msgs and send them out, batched per organize_msgs_by_agents
            unordered_map<addr_t, MData> msgs_to_send = organize_msgs_by_agents(active, db_.get_iter(), db_.out_graph_msgs);
            // Iterate through our neighbors and send all messages
            for (addr_t agent : agents()) {
                auto req = get_req(agent);
                if (msgs_to_send.count(agent) == 0) {
                    throw runtime_error("Agent changed during execution; not supported");
                } else {
                    MData& data = msgs_to_send[agent];
                    req->send(data.get(), data.size());
                }
            }

            // Processing and sending is done; we need to wait and receive all messages intended for us next
        }
        void alg_reset_state() {
            db_.teardown_graph();
            alg_msgs_recvd_at_iter_.clear();
            alg_active_at_iter_.clear();
        }
        void recv_alg_internal_compute([[maybe_unused]] zmq_socket_t sock, const char* data, const char* end) {
            // Convert all of the received messages back into graph_msgs
            bool active;
            itkey_t rcv_msg_iter;
            unpack_single(data, active);
            unpack_single(data, rcv_msg_iter);

            local_process_again_ |= active;

            // Process all received messages to vertices
            vector<tuple<graph_t, vtx_t, MData>> new_data = split_mdata_to_mdatas(data, end);

            for (auto [graph_id, vtx, dat] : new_data) {
                db_.in_graph_msgs[graph_id][rcv_msg_iter][vtx].push_back(dat);
            }

            // Process neighbor-level parts of the message (how many received, whether neighbor is active)
            ++alg_msgs_recvd_at_iter_[rcv_msg_iter];
            alg_active_at_iter_[rcv_msg_iter] |= active;

            // Move to the next processing loop if we have received all messages
            if (alg_msgs_recvd_at_iter_[db_.get_iter()] == num_neighbors()) {
                // We do different things if active or not
                if (alg_active_at_iter_[db_.get_iter()]) {
                    // If active, continue with next iteration computation
                    return alg_main_loop();
                } else {
                    // If not, we are finished and so we move onwards to close the stage
                    // Note that this also means everyone is not active at this iteration and so everyone will close
                    // Teardown graph datastructures and finish the stage
                    alg_reset_state();

                    return finish_db_process();
                }
            }

            // >>> TMP <<< Jan15 update:
            // we need to finish the internal loop process by putting data into the right place and then once we have received P messages from our P neighbors, we need to re-call alg_main_loop
            // when there are no additional messages being sent and every filter returns inactive (e.g., compute_internal returns 0) then we send a final 0-size message to every other peer (so that it is a 'global' zero message (FIXME think about whether this is correct)) then it exists
            //      this is probably not completely sufficient, we should probably not send "empty" messages but instead send a message to all peers saying whether we are active or not
            //      this is line 590 above
            // we want to make the additional changing of making group access have in- vs out- msgs, so that it is clean and clear whether a message is received or sent
        }
        void end_barrier_stage_alg_process() {
            // Prepare and send out vertices
            send_vertices_to_neighbors();

            // Just wait until we have all messages; do nothing more here
        }

        void recv_end_barrier([[maybe_unused]] zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            // The barrier is over, we have not done anything inside of the barrier
            // So, zero out the sent counters
            sent_distribution_.clear();

            // Proceed to the next state, depending on current state
            if (state_ == STAGE_BEGIN) {
                end_barrier_stage_begin();
            }
            else if (state_ == STAGE_PROCESSED) {
                end_barrier_stage_processed();
            }
            else if (state_ == STAGE_CLOSING) {
                end_barrier_stage_closing();
            }
            else if (state_ == STAGE_ALG_PROCESS) {
                end_barrier_stage_alg_process();
            }
            else throw runtime_error("End barrier at unknown stage");
        }

        void broadcast_process() {
            msg_type_t msg = PROCESS;
            pub((char*)&msg, sizeof(msg));

            //also send to self
            auto req = get_req(addr_.serialize());
            req->send(PROCESS);
        }

        void broadcast_algprocess() {
            msg_type_t msg = ALGPROCESS;
            pub((char*)&msg, sizeof(msg));

            //also send to self
            auto req = get_req(addr_.serialize());
            req->send(ALGPROCESS);
        }

        /** @brief Tell all neighbors (and self) to start processing */
        void recv_process_broadcast(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            // The state will get changed when the actual process message is
            // received anyway. However, let's set it here also to make the
            // "processing" function easier to use (i.e. ensure there's no gap
            // where that returns false, but really we're already processing)
            state_ = STAGE_BEGIN;

            broadcast_process();

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Call process on the internal DB */
        void recv_process(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            has_alg_processed_ = false;

            // First, enter STAGE_BEGIN
            state_ = STAGE_BEGIN;

            // Then, wait for a barrier
            start_barrier_wait();

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Start the internal alg db process mechanism */
        void recv_algprocess(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            state_ = STAGE_ALG_PROCESS;

            // Then, wait for a barrier
            start_barrier_wait();

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Query the internal DB */
        void recv_query(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            size_t num_tags;
            unpack_single(data, num_tags);
            vector<string> tags;
            for(size_t i = 0; i < num_tags; i++) {
                string tag {data};
                data += tag.size() + 1;
                tags.push_back(tag);
            }

            auto entry_keys = db_.query(tags);

            size_t msg_size = db_.serialize_size(entry_keys);
            char* msg = new char[msg_size];
            char *msg_ptr = msg;

            db_.serialize_entries(msg_ptr, entry_keys);

            send(sock, msg, msg_size);

            delete [] msg;
        }

        void recv_processing(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            bool is_processing = state_ != PRELOAD;

            size_t msg_size = sizeof(bool);
            char msg[msg_size];
            char *msg_ptr = msg;
            pack_single(msg_ptr, is_processing);

            send(sock, msg, msg_size);

        }

        /** @brief Insert an entry into the internal DB */
        void recv_add_entry(zmq_socket_t sock, const char* data, const char* end) {
            const char* data_start = data;
            DBEntry<> e {data};

            auto k1 = e.get_key();
            db_.verify_entry_key(&e);
            if (k1 != e.get_key()) throw runtime_error("Updating key not working in pardb"); // FIXME this can modify e, but the modifications are not preserved if forwarding

            // Check if we are the owner of the data
            if (has_ownership(e.get_key())) {
                update_recv_dist();

                // If so, add to our database
                if (state_ == STAGE_CLOSING || state_ == PRELOAD) {
                    db_.add_entry_worker(move(e));
                }
                else {
                    db_.stage_add_entry_worker(move(e));
                }
            } else {
                if (state_ != PRELOAD) { cout << "state was not preload, ERROR" << endl; throw runtime_error("Unimplemented");}
                // If not, simply forward to the owner
                auto req = find_req(e.get_key());
                const char* full_msg = get_full_msg(data_start);
                size_t size = end-full_msg;
                req->send(full_msg, size);
            }

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Add a tag to an existing entry, either here or forward */
        void recv_add_tag_to_entry(zmq_socket_t sock, const char* data, const char* end) {
            dbkey_t key;
            unpack_single(data, key);
            string tag {data, end};

            if (has_ownership(key)) {
                update_recv_dist();

                // We can only process if we are in a state of having finished
                // our process() call, and currently closing the stage
                if (state_ == STAGE_CLOSING) {
                    db_.add_tag_to_entry(key, tag);
                }
                else {
                    db_.stage_add_tag_worker(key, tag);
                }
            } else {
                // If not, simply forward to the owner
                if (state_ != PRELOAD) throw runtime_error("Unimplemented");
                auto req = find_req(key);
                const char* full_msg = get_full_msg(data);
                size_t size = end-full_msg;
                req->send(full_msg, size);
            }

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }
        
        /** @brief Remove a tag from an existing entry, either here or forward */
        void recv_remove_tag_from_entry(zmq_socket_t sock, const char* data, const char* end) {
            dbkey_t key;
            unpack_single(data, key);
            string tag {data, end};

            if (has_ownership(key)) {
                update_recv_dist();

                // We can only process if we are in a state of having finished
                // our process() call, and currently closing the stage
                if (state_ == STAGE_CLOSING || state_ == PRELOAD) db_.remove_tag_from_entry(key, tag);
                else db_.stage_remove_tag_worker(key, tag);
            } else {
                // If not, simply forward to the owner
                if (state_ != PRELOAD) throw runtime_error("Unimplemented");
                auto req = find_req(key);
                const char* full_msg = get_full_msg(data);
                size_t size = end-full_msg;
                req->send(full_msg, size);
            }

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);

        }

        /** @brief Update the value of an existing entry, either here or forward */
        void recv_update_entry_val(zmq_socket_t sock, const char* data, const char* end) {
            dbkey_t key;
            unpack_single(data, key);
            string val {data, end};

            if (has_ownership(key)) {
                update_recv_dist();

                // We can only process if we are in a state of having finished
                // our process() call, and currently closing the stage
                if (state_ == STAGE_CLOSING || state_ == PRELOAD) db_.update_entry_val(key, val);
                else db_.stage_update_entry_val_worker(key, val);
            } else {
                // If not, simply forward to the owner
                if (state_ != PRELOAD) throw runtime_error("Unimplemented");
                auto req = find_req(key);
                const char* full_msg = get_full_msg(data);
                size_t size = end-full_msg;
                req->send(full_msg, size);
            }

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);

        }

        /** @brief Subscribe to the creation of an entry, either here or forward */
        void recv_subscribe_to_entry(zmq_socket_t sock, const char* data, const char* end) {
            dbkey_t my_key;
            dbkey_t wait_key;
            unpack_single(data, my_key);
            unpack_single(data, wait_key);
            string tag {data, end};

            //the owner of the wait key will be the one to trigger the tag
            //remove upon entry creation
            if (has_ownership(wait_key)) {
                update_recv_dist();

                db_.subscribe_to_entry(my_key, wait_key, tag);
            } else {
                // If not, simply forward to the owner
                if (state_ != PRELOAD) throw runtime_error("Unimplemented");
                auto req = find_req(wait_key);
                const char* full_msg = get_full_msg(data);
                size_t size = end-full_msg;
                req->send(full_msg, size);
            }

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);

        }

        void recv_db_size(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            size_t res = db_size();

            size_t msg_size = sizeof(size_t);
            char msg[msg_size];
            char *msg_ptr = msg;
            pack_single(msg_ptr, res);

            send(sock, msg, msg_size);
        }

        void recv_get_state(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            string ret = "";
            if (state_ == PRELOAD)
                ret = "PRELOAD";
            if (state_ == STAGE_BEGIN)
                ret = "STAGE_BEGIN";
            if (state_ == STAGE_PROCESSED)
                ret = "STAGE_PROCESSED";
            if (state_ == STAGE_CLOSING)
                ret = "STAGE_CLOSING";
            if (state_ == STAGE_ALG_PROCESS)
                ret = "STAGE_ALG_PROCESS";

            size_t msg_size = sizeof(msg_type_t) + ret.size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_string_null_term(msg_ptr, ret);

            send(sock, msg, msg_size);

            delete [] msg;
                

        }

        /** @brief Add filter directory into the seq DB */
        void add_filter_dir(string dir) {
            db_.add_filter_dir(dir);
        }

        /** @brief Install a filter in the seq DB */
        void install_filter(string f) {
            db_.install_filter(f);
        }

        /** @brief Load a text file of entries into the DB */
        void add_db_file(string f) {
            db_.add_db_file(f);
        }

        /** @brief Get a copy of the internal DB (For testing only)*/
        absl::flat_hash_map<dbkey_t, DBEntry<>> entries() { return db_.entries(); }

        unordered_map<vtx_t, msg_t> graph_msgs;

        unordered_map<addr_t, MData> organize_msgs_by_agents(bool active, itkey_t iter, unordered_map<graph_t, msg_t>& graph_msgs) {
            // Go through our internal db and get all groups and their messages at the current iteration for that group
            // NOTE: the iteration value is set by the compute_internal method
            // and guaranteed for every vertex to have the same iteration, and
            // also for the iterations to be incremented. Therefore, we just
            // take the last iteration as the current iteration.

            unordered_map<addr_t, vector<tuple<graph_t, vtx_t, MData*>>> loosely_packed_output;
            unordered_map<addr_t, size_t> loosely_packed_size;

            // Make sure all neighbors are in the output, even if they have no data
            for (addr_t agent : agents()) {
                loosely_packed_output[agent];       // Create the empty vector so we keep track of all neighbors and send empty values that show our state
                if (loosely_packed_output.count(agent) != 1) throw runtime_error("Something did not work here, agent output data was not default initialized");
            }

            for (auto & [graph_id, msgs] : graph_msgs) {
                for (auto & [vtx_id, mdata] : msgs[iter]) {
                    auto & mdatas = msgs[iter][vtx_id];
                    for (auto & mdata : mdatas) {
                        dbkey_t lookup_key {graph_id, vtx_id, 0};
                        addr_t neigh_addr = lookup_agent(lookup_key);
                        loosely_packed_output[neigh_addr].push_back({graph_id, vtx_id, &mdata});
                        loosely_packed_size[neigh_addr] += sizeof(graph_id)+sizeof(vtx_id)+mdata.serialized_size();
                    }
                }
            }

            unordered_map<addr_t, MData> final_output;

            for (auto & [addr, mdata_ptrs] : loosely_packed_output) {
                // Find the total size
                size_t total_size = loosely_packed_size[addr]+sizeof(msg_type_t)+sizeof(itkey_t)+sizeof(bool);

                // Allocate the space
                MData data;
                data.resize(total_size);
                char* buffer = data.get();
                pack_msg(buffer, ALG_INTERNAL_COMPUTE);
                pack_single(buffer, active);
                pack_single(buffer, iter);

                // Copy the data
                for (auto [graph_id, vtx_id, mdata] : mdata_ptrs) {
                    pack_single(buffer, graph_id);
                    pack_single(buffer, vtx_id);
                    mdata->serialize(buffer);
                }

                // Save it to the output
                final_output[addr] = data;
            }
            return final_output;
        }

        // We are a single agent and .. are given one single MData which contains all of the smaller MDatas
        // Break the single MData apart into MDatas for each "vertex"
        static vector<tuple<graph_t, vtx_t, MData>> split_mdata_to_mdatas(const char* buffer, const char* buf_end) {
            vector<tuple<graph_t, vtx_t, MData>> res;
            // Iterate through the MData

            while (buffer < buf_end) {
                // First get the vtx_id
                vtx_t vtx_id;
                graph_t graph_id;
                unpack_single(buffer, graph_id);
                unpack_single(buffer, vtx_id);

                // Second build the new MData
                res.emplace_back();
                get<0>(res[res.size()-1]) = graph_id;
                get<1>(res[res.size()-1]) = vtx_id;
                get<2>(res[res.size()-1]).from_serialized(buffer);
            }

            return res;
        }


        void send_vertices_to_neighbors() {
            // Send out all vertices to everyone as appropriate
            unordered_map<addr_t, set<tuple<graph_t, vtx_t>>> vertices_to_send;

            for (addr_t agent : agents()) {
                vertices_to_send[agent];
            }

            vector<dbkey_t> ret_entry_keys;
            if (!skip_group_filters_) {
                ret_entry_keys = db_.keys();
            }
            for (auto & k : ret_entry_keys) {
                // Get the address of the host that owns k.b (should always be "us") and k.c
                dbkey_t lookup_key_b {k.a, k.b, 0};
                addr_t neigh_addr_b = lookup_agent(lookup_key_b);
                vertices_to_send[neigh_addr_b].insert({k.a, k.b});

                dbkey_t lookup_key_c {k.a, k.c, 0};
                addr_t neigh_addr_c = lookup_agent(lookup_key_c);
                vertices_to_send[neigh_addr_c].insert({k.a, k.c});
            }

            for (auto & [addr, vtx_set] : vertices_to_send) {
                // send out the list of vertices to each neighbor
                size_t num_vtxs = vtx_set.size();
                size_t msg_size = sizeof(msg_type_t) + sizeof(size_t) + num_vtxs*(sizeof(graph_t)+sizeof(vtx_t));
                char* msg = new char[msg_size];
                char *msg_ptr = msg;

                pack_msg(msg_ptr, ALG_VERTICES);
                pack_single(msg_ptr, num_vtxs);
                for (auto & [graph_id, vtx] : vtx_set) {
                    pack_single(msg_ptr, graph_id);
                    pack_single(msg_ptr, vtx);
                }

                auto req = get_req(addr);
                req->send(msg, msg_size);

                delete [] msg;
            }
        }

        void recv_alg_vertices([[maybe_unused]] zmq_socket_t sock, const char* data, [[maybe_unused]] const char* end) {
            size_t num_vtxs;
            unpack_single(data, num_vtxs);

            for (size_t i = 0; i < num_vtxs; ++i) {
                graph_t graph_id;
                vtx_t vtx;
                unpack_single(data, graph_id);
                unpack_single(data, vtx);

                db_.add_alg_vertex({graph_id, vtx});
            }

            // Count how many received; once we have all messages from all neighbors, proceed to next step
            if (++alg_vertex_msgs_received_ == num_neighbors()) {
                // Reset the counter
                alg_vertex_msgs_received_ = 0;

                // Start the alg db processing mechanism
                db_.setup_graph_datastructures_have_ids();

                // Start the internal loop
                alg_main_loop();
            }

        }

        void recv_print_entries(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {

            for (size_t i = 0; i < 100; i++) {
                cout << "DEBUGGING" << endl;
            }

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }
};

void s_ref_add_entry(fn_ref r, DBEntry<> e) {
    ParDB* db = (ParDB*)r;
    db->add_entry(move(e));
}
void s_ref_add_tag_to_entry(fn_ref r, dbkey_t key, string tag) {
    ParDB* db = (ParDB*)r;
    db->add_tag_to_entry(key, tag);
}
void s_ref_remove_tag(fn_ref r, dbkey_t key, string tag) {
    ParDB* db = (ParDB*)r;
    db->remove_tag_from_entry(key, tag);
}
void s_ref_update_entry_val(fn_ref r, dbkey_t key, string val) {
    ParDB* db = (ParDB*)r;
    db->update_entry_val(key, val);
}
void s_ref_subscribe_to_entry(fn_ref r, dbkey_t my_key, dbkey_t wait_key, string inactive_tag) {
    ParDB* db = (ParDB*)r;
    db->subscribe_to_entry(my_key, wait_key, inactive_tag);
}
void s_ref_get_entry_by_key(fn_ref r, dbkey_t search_key, char** res) {
    ParDB* db = (ParDB*)r;
    db->get_entry_by_key(search_key, res);
}
}
