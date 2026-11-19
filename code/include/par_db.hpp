#pragma once

#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <list>
#include <memory>
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
#include "par_db_options.hpp"
#include "par_db_thread.hpp"
#include "par_db_responder.hpp"
#include "process_stats.hpp"

using namespace std;
using namespace elga;

namespace pando {

void s_ref_add_entry(fn_ref, DBEntry<>);
void s_ref_add_tag_to_entry(fn_ref, dbkey_t, string);
void s_ref_remove_tag(fn_ref, dbkey_t, string);
void s_ref_update_entry_val(fn_ref, dbkey_t, string);
void s_ref_subscribe_to_entry(fn_ref, dbkey_t, dbkey_t, string);
void s_ref_get_entry_by_key(fn_ref, dbkey_t, char**);
void s_ref_get_entries_by_keys(fn_ref, size_t, const dbkey_t*, char**);

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

        /** @brief Done with local work and waiting at a barrier for the
         * rest of the mesh (reported to pando_top to spot stragglers) */
        bool at_barrier_ = false;

        ParDBThread<PandoResponder> responder_;

        bool skip_group_filters_ = false;

        /** @brief The CLOSE_EARLY requests other agents have sent. Requests
         * can arrive before this agent starts that pass, so they are
         * remembered rather than matched as they are received. */
        CloseEarlyState close_early_;
        /** @brief Last pass this agent asked everyone to close early, so it
         * asks at most once per pass */
        uint64_t close_early_sent_pass_ = 0;

        /** @brief Slurm's tasks per node (read at startup); 0 if not under
         * Slurm or not known */
        uint64_t slurm_tasks_per_node_ = 0;
        /** @brief Last agents-per-node answer, to log when it changes */
        uint64_t logged_agents_per_node_ = 0;

        /** @brief The agents sharing this node's memory, for the memory
         * budget: Slurm's tasks per node if known, else the agents in the
         * mesh with our IP address (at least ourselves) */
        uint64_t agents_on_node_() {
            uint64_t n = slurm_tasks_per_node_;
            const char* from = "slurm";
            if (n == 0) {
                n = count_agents_sharing_ip(agents(), addr_ser_);
                from = "mesh";
            }
            if (n == 0) n = 1;
            if (n != logged_agents_per_node_) {
                logged_agents_per_node_ = n;
                cerr << "[Pando] [INFO] memory budget is shared by " << n << " agent(s) on this node (from "
                     << from << ")" << endl;
            }
            return n;
        }

        static ParDBOptions with_continuous_processing_(ParDBOptions opts, bool continuous_processing) {
            opts.continuous_processing = continuous_processing;
            return opts;
        }

        static ParDBOptions with_write_batch_bytes_(ParDBOptions opts, size_t write_batch_bytes) {
            opts.write_batch_bytes = write_batch_bytes;
            return opts;
        }

        static ParDBOptions make_options_(bool skip_group_filters, size_t max_filters_per_stage,
                                          const MemoryPolicy& mem_policy) {
            ParDBOptions opts;
            opts.skip_group_filters = skip_group_filters;
            opts.max_filters_per_stage = max_filters_per_stage;
            opts.mem_policy = mem_policy;
            return opts;
        }

        /** @brief Most keys get_entries_by_keys puts in one request to one
         * agent (see set_max_keys_per_request) */
        size_t max_keys_per_request_ = 65536;

        /** @brief Write batching
         *
         * While filters run (see start_db_process), the writes they make
         * (new entries, tag adds and removes, value updates, subscriptions)
         * are not sent one message each. They are appended, in the order they
         * are made, to one buffer per owning agent, and each buffer is sent
         * as a single WRITE_BATCH message once it reaches write_batch_bytes_,
         * plus a final flush when the filters finish. One buffer per owner
         * keeps every owner's writes in the same order as unbatched messages
         * (one connection per owner is already in order).
         *
         * Batching matters because nothing a receiver is sent during a pass
         * is read until the pass ends: each small message would otherwise
         * keep most of a ZeroMQ receive buffer (~8 KB) allocated meanwhile.
         *
         * The barrier counts messages, so a batch counts as one message on
         * both sides. Every batch must be flushed before the barrier starts
         * (checked in start_barrier_wait).
         */
        enum write_op_t : uint8_t {
            WRITE_OP_ADD_ENTRY = 1,     // [DBEntry serialized]
            WRITE_OP_ADD_TAG,           // [dbkey_t key][size_t len][tag]
            WRITE_OP_REMOVE_TAG,        // [dbkey_t key][size_t len][tag]
            WRITE_OP_UPDATE_VAL,        // [dbkey_t key][size_t len][value]
            WRITE_OP_SUBSCRIBE          // [dbkey_t my_key][dbkey_t wait_key][size_t len][tag]
        };
        /** WRITE_BATCH layout: [msg_type_t][size_t count][count x (uint8_t op, fields)] */
        static constexpr size_t WRITE_BATCH_HEADER = sizeof(msg_type_t) + sizeof(size_t);

        /** @brief An owner's requester (as returned by find_req) */
        using req_iter_t = decltype(std::declval<PandoParticipant&>().find_req(std::declval<dbkey_t>()));

        struct WriteBatch {
            req_iter_t req;         // the owner's requester
            vector<char> buf;       // header (filled in on flush) + records
            size_t count = 0;       // records in buf
        };
        absl::flat_hash_map<addr_t, WriteBatch> write_batches_;
        /** @brief Set while filters run in start_db_process */
        bool batching_writes_ = false;
        /** @brief Flush an owner's batch once it reaches this many bytes;
         * 0 turns batching off (see set_write_batch_bytes) */
        size_t write_batch_bytes_ = DEFAULT_WRITE_BATCH_BYTES;
        /** @brief WRITE_BATCH messages sent and the writes they carried,
         * since this agent started (read by tests from other threads) */
        std::atomic<uint64_t> write_batches_sent_ {0};
        std::atomic<uint64_t> writes_batched_ {0};

    public:
        /** @brief Default flush size for write batches */
        static constexpr size_t DEFAULT_WRITE_BATCH_BYTES = ParDBOptions::DEFAULT_WRITE_BATCH_BYTES;

        /** @brief Initialize the parallel DB
         *
         * @param max_filters_per_stage if nonzero, how many filter runs one
         *        processing pass may do before the stage closes (see
         *        SeqDB::set_max_filters_per_stage); 0 keeps the default (unlimited)
         * @param mem_policy when to end a pass early to bound memory (see
         *        memory_policy.hpp); enabled by default, MemoryPolicy::disabled()
         *        turns it off. Requests from other agents to close early are
         *        honored either way: right away if the asking agent ran a
         *        filter in the pass, else once a filter has run here. An
         *        agents_per_node of 0 uses Slurm's tasks per node, else the
         *        agents in the mesh with this agent's IP address.
         */
        ParDB(ZMQAddress addr, size_t sz, bool skip_group_filters=false, size_t max_filters_per_stage=0,
                const MemoryPolicy& mem_policy=MemoryPolicy{}) :
                ParDB(addr, sz, make_options_(skip_group_filters, max_filters_per_stage, mem_policy)) { }

        /** @brief Initialize the parallel DB, setting the write batch flush size
         * (see set_write_batch_bytes; 0 turns batching off) and whether
         * processing is continuous (see SeqDB::set_continuous_processing) */
        ParDB(ZMQAddress addr, size_t sz, bool skip_group_filters, size_t max_filters_per_stage,
              size_t write_batch_bytes, bool continuous_processing=true) :
                ParDB(addr, sz, with_continuous_processing_(with_write_batch_bytes_(make_options_(skip_group_filters,
                      max_filters_per_stage, MemoryPolicy{}), write_batch_bytes), continuous_processing)) { }

        /** @brief Initialize the parallel DB with the given settings (see ParDBOptions) */
        ParDB(ZMQAddress addr, size_t sz, const ParDBOptions& opts) :
                PandoParticipant(addr, true),
                db_refs_(this,
                    s_ref_add_entry,
                    s_ref_add_tag_to_entry,
                    s_ref_remove_tag,
                    s_ref_update_entry_val,
                    s_ref_subscribe_to_entry,
                    s_ref_get_entry_by_key,
                    s_ref_get_entries_by_keys
                ),
                db_(db_refs_, addr_ser_, sz),
                state_(PRELOAD),
                sync_sent_dist_count_(0),
                sync_recv_at_barrier_(0),
                local_sent_dist_(0),
                local_recv_dist_(0),
                responder_(addr + RESPONDER_LOCALNUM_OFFSET),
                skip_group_filters_(opts.skip_group_filters),
                write_batch_bytes_(opts.write_batch_bytes) {
            sub(PROCESS);
            sub(ALGPROCESS);
            sub(END_BARRIER);
            sub(ADD_FILTER_DIR);
            sub(INSTALL_FILTER);
            sub(CLEAR_FILTERS);
            sub(EXPORT_DB);
            sub(EXPORT_DB_WITH_TAG);

            if (skip_group_filters_) db_.disable_group_filters();
            if (opts.max_filters_per_stage != 0) db_.set_max_filters_per_stage(opts.max_filters_per_stage);
            if (opts.scan_block_keys != 0) db_.set_scan_block_keys(opts.scan_block_keys);
            db_.set_continuous_processing(opts.continuous_processing);
            db_.set_memory_policy(opts.mem_policy);
            slurm_tasks_per_node_ = slurm_tasks_per_node();
            db_.set_agents_per_node_source([this]() { return agents_on_node_(); });

            // The message loop does not run while filters run, so publish
            // progress from inside the filter loops instead
            db_.set_progress_callback([this]() { publish_progress(); });

            // Likewise, requests to close early are checked from inside the
            // filter loop. They arrive on the control subscription, which the
            // message loop does not read, so checking cannot take any other
            // message off the main subscription.
            ctrl_sub(CLOSE_EARLY);
            db_.set_stop_requested_callback([this](bool have_run) { return close_early_requested_(have_run); });
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

            publish_progress();

            // Done with our filters and waiting on the rest of the mesh, whose
            // output keeps arriving here until the stage closes: if memory is
            // over the limit now, ask everyone to stop. We may have run no
            // filter this pass, and then the request only stops agents that
            // have (see CloseEarlyState).
            if (state_ == STAGE_PROCESSED && db_.under_memory_pressure())
                publish_close_early_(STOP_MEM_PRESSURE, db_.filters_run_last_pass() > 0);
        }

        /** @brief Ask every agent to end the current pass early (once per pass)
         * @param ran_filter whether this agent ran a filter in the pass */
        void publish_close_early_(uint8_t reason, bool ran_filter) {
            uint64_t pass = db_.stats().pass;
            if (pass == 0 || close_early_sent_pass_ == pass) return;
            close_early_sent_pass_ = pass;

            #ifdef VERBOSE
            cerr << "[Pando] asking all agents to close pass " << pass << " early (" << stop_reason_name(reason) << ")" << endl;
            #endif
            CloseEarlyRequest req;
            req.agent = addr_ser_;
            req.pass = pass;
            req.reason = reason;
            req.ran_filter = ran_filter;
            StatsWriter w { CLOSE_EARLY };
            req.serialize(w);
            pub(w.data(), w.size());
        }

        /** @brief Read any CLOSE_EARLY requests and return whether they end
         * the current pass (see CloseEarlyState::should_stop)
         *
         * Every agent advances ProcessStats::pass on the same PROCESS and
         * ALGPROCESS broadcasts, so pass numbers agree across the mesh. (An
         * agent that joins after processing has started can disagree; at
         * worst it then ignores a request or stops one pass early, both of
         * which are safe.)
         *
         * @param have_run whether this agent has run a filter in the pass
         */
        bool close_early_requested_(bool have_run) {
            drain_ctrl([this](const char* data, const char* end) {
                msg_type_t type = unpack_msg(data);
                if (type != CLOSE_EARLY) return;
                CloseEarlyRequest req;
                if (!req.deserialize(data, end)) return;
                // Publications reach our own subscription too; record skips them
                close_early_.record(req, addr_ser_);
            });
            return close_early_.should_stop(db_.stats().pass, have_run);
        }

        /** @brief Publish a PROGRESS message describing our processing */
        void publish_progress() {
            ProgressSnapshot p;
            p.agent = addr_ser_;
            p.time_ms = wall_ms();
            p.db_size = db_size();
            p.state = state_;
            p.at_barrier = at_barrier_;
            p.set_stats(db_.stats());
            // During a stage close we reach the barrier as soon as the staged
            // changes are sent to ourselves, but applying them happens after
            // that, as the messages are received. Until they are all applied
            // we are still working, not waiting on the rest of the mesh.
            if (state_ == STAGE_CLOSING && p.close.remaining() > 0)
                p.at_barrier = false;

            StatsWriter w { PROGRESS };
            p.serialize(w);
            pub(w.data(), w.size());
        }

        /** @brief Publish a STAGE_EVENT message */
        void publish_stage_event(const StageEvent& e) {
            StatsWriter w { STAGE_EVENT };
            e.serialize(w);
            pub(w.data(), w.size());
        }

        /** @brief Change state, publishing the transition */
        void set_state_(state_t new_state) {
            if (new_state == state_) return;
            StageEvent e;
            e.agent = addr_ser_;
            e.time_ms = wall_ms();
            e.kind = EVENT_STATE_CHANGE;
            e.from_state = state_;
            e.to_state = new_state;
            e.summary.pass = db_.stats().pass;
            e.summary.kind = db_.stats().pass_kind;
            state_ = new_state;
            publish_stage_event(e);
        }

        /** @brief Publish a summary of the pass that just finished and
         * fold it into the totals */
        void end_pass_() {
            auto& stats = db_.stats();
            if (stats.pass_kind == PASS_NONE) return;
            StageEvent e;
            e.agent = addr_ser_;
            e.time_ms = wall_ms();
            e.kind = EVENT_PASS_END;
            e.from_state = state_;
            e.to_state = state_;
            e.summary = stats.current_summary();
            publish_stage_event(e);
            stats.end_pass();
        }

        /** @brief Handle specific, custom messages */
        virtual void process_msg(msg_type_t type, zmq_socket_t sock, const char *data, [[maybe_unused]] const char* end) {
            if (type == ADD_ENTRY)
                recv_add_entry(sock, data, end);
            else if (type == WRITE_BATCH)
                recv_write_batch(sock, data, end);
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
            else if (type == EXPORT_DB_WITH_TAG)
                recv_export_db_with_tag(sock, data, end);
            else if (type == EXPORT_DB_WITH_TAG_BROADCAST)
                recv_export_db_with_tag_broadcast(sock, data, end);
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

        /** @brief Keys the filter scan fetches from the map per request */
        size_t scan_block_keys() const { return db_.scan_block_keys(); }

        /** @brief Whether a process request keeps sweeping until nothing runs */
        bool continuous_processing() const { return db_.continuous_processing(); }

        /** @brief Processing passes this agent has started (for tests) */
        uint64_t passes_started() const { return db_.stats().pass; }

        /** @brief Entries this agent has offered to its filters (for tests) */
        size_t entries_examined() const { return db_.entries_examined(); }


        /** @brief Set the flush size for write batches (bytes per owning
         * agent; default DEFAULT_WRITE_BATCH_BYTES). 0 turns batching off, so
         * every write is sent as its own message, as before. Takes effect at
         * the next processing pass. */
        void set_write_batch_bytes(size_t n) { write_batch_bytes_ = n; }

        /** @brief Return the flush size for write batches (0 = off) */
        size_t write_batch_bytes() const { return write_batch_bytes_; }

        /** @brief WRITE_BATCH messages this agent has sent */
        uint64_t write_batches_sent() const { return write_batches_sent_.load(); }

        /** @brief Writes this agent has sent inside WRITE_BATCH messages */
        uint64_t writes_batched() const { return writes_batched_.load(); }

    private:
        /** @brief Make room for one record of rec_size bytes at the end of
         * the batch for key's owner; returns where to write it and sets owner */
        char* batch_append_(dbkey_t key, size_t rec_size, addr_t& owner) {
            auto req = find_req(key);
            owner = req->addr();
            WriteBatch& b = write_batches_[owner];
            if (b.buf.empty()) {
                b.req = req;
                b.buf.reserve(min(write_batch_bytes_, (size_t)(1u << 20)) + rec_size + WRITE_BATCH_HEADER);
                b.buf.resize(WRITE_BATCH_HEADER);   // filled in by flush_write_batch_
            }
            size_t off = b.buf.size();
            b.buf.resize(off + rec_size);
            ++b.count;
            return b.buf.data() + off;
        }

        /** @brief Send owner's batch if it has reached write_batch_bytes_ */
        void flush_write_batch_if_full_(addr_t owner) {
            auto it = write_batches_.find(owner);
            if (it != write_batches_.end() && it->second.buf.size() >= write_batch_bytes_)
                flush_write_batch_(it->second);
        }

        /** @brief Send a batch as one WRITE_BATCH message (counted as one
         * message for the barrier) and empty it */
        void flush_write_batch_(WriteBatch& b) {
            if (b.count == 0) return;
            char* p = b.buf.data();
            pack_msg(p, WRITE_BATCH);
            pack_single(p, b.count);
            b.req->send(b.buf.data(), b.buf.size());
            update_sent_dist(b.req->addr());
            ++write_batches_sent_;
            writes_batched_ += b.count;
            b.buf.clear();
            b.count = 0;
        }

        /** @brief Send every batch and free the buffers */
        void flush_all_write_batches_() {
            for (auto & [owner, b] : write_batches_)
                flush_write_batch_(b);
            write_batches_.clear();
        }

        /** @brief Batch a [op][key][size_t len][str] record (tag add/remove,
         * value update) for key's owner */
        void batch_key_string_(write_op_t op, dbkey_t key, const string& str) {
            addr_t owner;
            char* p = batch_append_(key, sizeof(uint8_t) + sizeof(dbkey_t) + sizeof(size_t) + str.size(), owner);
            pack_single(p, (uint8_t)op);
            pack_single(p, key);
            pack_single(p, str.size());
            memcpy(p, str.data(), str.size());
            flush_write_batch_if_full_(owner);
        }

    public:
        /** @brief Adds an entry to the global DB */
        void add_entry(DBEntry<> entry) {
            //Before we forward the entry, make sure it no longer has the INITIAL_KEY
            db_.verify_entry_key(&entry);

            dbkey_t key = entry.get_key();

            size_t ser_size = entry.serialize_size();

            if (batching_writes_) {
                addr_t owner;
                char* p = batch_append_(key, sizeof(uint8_t) + ser_size, owner);
                pack_single(p, (uint8_t)WRITE_OP_ADD_ENTRY);
                entry.serialize(p);
                flush_write_batch_if_full_(owner);
                return;
            }

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
            if (batching_writes_) { batch_key_string_(WRITE_OP_ADD_TAG, key, tag); return; }

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
            if (batching_writes_) { batch_key_string_(WRITE_OP_REMOVE_TAG, key, tag); return; }

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
            if (batching_writes_) { batch_key_string_(WRITE_OP_UPDATE_VAL, key, tag); return; }

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
            if (batching_writes_) {
                // Routed to the owner of wait_key, which triggers the tag removal
                addr_t owner;
                char* p = batch_append_(wait_key, sizeof(uint8_t) + 2*sizeof(dbkey_t) + sizeof(size_t) + inactive_tag.size(), owner);
                pack_single(p, (uint8_t)WRITE_OP_SUBSCRIBE);
                pack_single(p, my_key);
                pack_single(p, wait_key);
                pack_single(p, inactive_tag.size());
                memcpy(p, inactive_tag.data(), inactive_tag.size());
                flush_write_batch_if_full_(owner);
                return;
            }

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


        /** @brief Lookup an entry by key
         *
         * A key this agent owns is read from its own PandoMap (through the
         * ParDB thread's map client, idle whenever a filter runs) instead of
         * through its own responder. The result is the same either way:
         * - a missing key or an empty value leaves *res unchanged (callers
         *   set it to nullptr first), as the remote path's "\0" comparison
         *   does;
         * - otherwise *res is a malloc'd copy, cut at the first NUL byte
         *   (strcpy), as the remote path does.
         */
        void get_entry_by_key(dbkey_t key, char** res) {
            if (has_ownership(key)) {
                string value;
                if (!db_.get_value_if_exists(key, value) || value.empty()) return;
                *res = (char*)malloc(sizeof(char)*value.size()+1);
                if (*res == nullptr) throw runtime_error("Unable to allocate memory");
                strcpy(*res, value.c_str());
                return;
            }

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

        /** @brief Look up many keys with one blocking call
         *
         * Keys are grouped by the agent that owns them. One request is sent
         * to every other owner's responder before any reply is awaited, so
         * the owners look their keys up in parallel; each answers its whole
         * group with a single PandoMap lookup. Keys this agent owns are read
         * directly from its own PandoMap while the other owners work, rather
         * than through its own responder.
         *
         * values[i] receives a malloc'd, NUL-terminated copy of the value
         * for keys[i] (the caller frees it), or nullptr if the key does not
         * exist. An empty value gives an empty string, not nullptr. Keys may
         * repeat. On error every value is freed and set to nullptr before the
         * exception is thrown.
         */
        void get_entries_by_keys(size_t n, const dbkey_t* keys, char** values) {
            for (size_t i = 0; i < n; ++i) values[i] = nullptr;
            if (n == 0) return;

            // Group the positions of the keys by owning agent, keeping this
            // agent's own keys apart
            unordered_map<addr_t, vector<size_t>> by_owner;
            vector<size_t> local_idx;
            for (size_t i = 0; i < n; ++i) {
                addr_t owner = lookup_agent(keys[i]);
                if (owner == addr_ser_) local_idx.push_back(i);
                else by_owner[owner].push_back(i);
            }

            // Each owner's keys are sent in requests of at most
            // max_keys_per_request_ keys. Every owner's first request goes
            // out before any reply is awaited; after each reply, that owner
            // is sent its next request.
            using req_it = decltype(find_req(keys[0], true));
            struct owner_t {
                req_it req;                   // the owner's responder
                const vector<size_t>* idx;    // positions of its keys
                size_t next = 0;              // first position of the next request
                size_t cur = 0;               // first position of the request in flight
                bool in_flight = false;       // a request was sent and its reply not read
            };

            // The first error (a failed send, a bad reply, or the local
            // lookup). After an error no new request is sent, but every
            // request already sent is still read, so no responder socket is
            // left with an unread reply; then all values are freed and the
            // error is rethrown.
            exception_ptr error;

            auto send_next = [&](owner_t& o) {
                size_t b = o.next;
                size_t e = min(b + max_keys_per_request_, o.idx->size());
                size_t count = e - b;

                size_t msg_size = sizeof(msg_type_t) + sizeof(size_t) + count*sizeof(dbkey_t);
                // Freed even if the send throws. Not zero-filled (unlike
                // vector<char>(n) or make_unique<char[]>(n)): every byte is
                // written below before it is sent.
                unique_ptr<char[]> msg(new char[msg_size]);
                char* msg_ptr = msg.get();
                pack_msg(msg_ptr, GET_ENTRIES_BY_KEYS);
                pack_single(msg_ptr, count);
                for (size_t j = b; j < e; ++j) pack_single(msg_ptr, keys[(*o.idx)[j]]);

                o.req->send(msg.get(), msg_size);

                o.cur = b;
                o.next = e;
            };

            vector<owner_t> owners;
            owners.reserve(by_owner.size());
            for (auto & [owner, idx] : by_owner) {
                owner_t o;
                o.req = find_req(keys[idx[0]], true);
                o.idx = &idx;
                owners.push_back(o);
            }
            for (auto & o : owners) {
                if (error) break;
                try {
                    send_next(o);
                    o.in_flight = true;
                } catch (...) {
                    error = current_exception();
                }
            }

            // While the other owners work, read this agent's own keys from its
            // PandoMap. This uses the ParDB thread's own map client, which is
            // idle whenever a filter runs. Call the SeqDB worker directly: the
            // virtual get_entry_values_by_keys on db_ comes back here.
            // An error is held until every remote reply has been read.
            // Local keys are read in the same size of request as remote ones.
            if (!error) try {
                vector<dbkey_t> local_keys;
                vector<char*> local_values;
                for (size_t b = 0; b < local_idx.size(); b += max_keys_per_request_) {
                    size_t e = min(b + max_keys_per_request_, local_idx.size());
                    local_keys.clear();
                    for (size_t j = b; j < e; ++j) local_keys.push_back(keys[local_idx[j]]);
                    local_values.assign(e - b, nullptr);
                    db_.get_entry_values_by_keys_worker(local_keys.size(), local_keys.data(), local_values.data());
                    for (size_t j = b; j < e; ++j) values[local_idx[j]] = local_values[j - b];
                }
            } catch (...) {
                error = current_exception();
            }

            // Collect the replies owner by owner in turns. Each reply is
            // parsed into values and freed as soon as it is read, so at most
            // one reply is held at a time; then that owner is sent its next
            // request.
            bool any_in_flight = true;
            while (any_in_flight) {
                any_in_flight = false;
                for (auto & o : owners) {
                    if (!o.in_flight) continue;
                    o.in_flight = false;
                    // A failed read (e.g. EINTR from a signal) must not stop the
                    // other owners' replies from being read; it is recorded like
                    // any other error. That one socket cannot be recovered.
                    try {
                        ZMQMessage msg = o.req->read();
                        if (!error) {
                            const char* data = msg.data();
                            unpack_values_reply(data, data + msg.size(), o.next - o.cur,
                                                o.idx->data() + o.cur, values);
                        }
                    } catch (...) {
                        if (!error) error = current_exception();
                    } // the reply is freed here
                    if (!error && o.next < o.idx->size()) {
                        try {
                            send_next(o);
                            o.in_flight = true;
                        } catch (...) {
                            error = current_exception();
                        }
                    }
                    any_in_flight |= o.in_flight;
                }
            }

            if (error) {
                for (size_t i = 0; i < n; ++i) { free(values[i]); values[i] = nullptr; }
                rethrow_exception(error);
            }
        }

        /** @brief Set the most keys get_entries_by_keys sends to one agent in
         * one request (default 65536). A larger lookup is split into several
         * requests to that agent; the caller still makes one call. This
         * bounds the size of each request and reply message.
         */
        void set_max_keys_per_request(size_t n) {
            if (n == 0) throw invalid_argument("max_keys_per_request must be at least 1");
            max_keys_per_request_ = n;
        }

        /** @brief Return the most keys get_entries_by_keys sends in one request */
        size_t max_keys_per_request() const { return max_keys_per_request_; }

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

        void recv_export_db_with_tag_broadcast(zmq_socket_t sock, const char* data, const char* end) {
            string data_str {data, end};

            size_t msg_size = sizeof(msg_type_t) + data_str.size();
            char* msg = new char[msg_size];
            char* msg_ptr = msg;

            pack_msg(msg_ptr, EXPORT_DB_WITH_TAG);
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
            db_.export_db_with_tag(dir + "/pando-export-" + addr_.get_addr_str(), tag);
        }

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
            // A write still in a batch would not be counted as sent, and the
            // barrier would wait for it forever (or close without it)
            if (!write_batches_.empty())
                throw logic_error("ParDB: batched writes were not flushed before the barrier");

            at_barrier_ = true;
            publish_progress();

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
            {
                // Batch the writes filters make while they run (see
                // write_batches_). The flag is cleared even if a filter throws.
                struct BatchingScope {
                    bool& flag;
                    BatchingScope(bool& f, bool on) : flag(f) { flag = on; }
                    ~BatchingScope() { flag = false; }
                } batching { batching_writes_, write_batch_bytes_ != 0 };
                local_process_again_ = db_.process();
            }
            #ifdef VERBOSE
            cerr << "finished process filters" << endl;
            #endif

            // We stopped to bound memory, but the other agents' output keeps
            // arriving here until every agent stops and the stage closes, so
            // ask them to stop too (first, so they stop as soon as possible).
            // Both reasons apply only after a filter run, so this pass has
            // made progress here and the others may stop even if they have
            // run nothing yet.
            uint8_t reason = db_.stats().stop_reason;
            if (reason == STOP_MEM_BUDGET || reason == STOP_MEM_PRESSURE)
                publish_close_early_(reason, true);

            // Everything sent during the pass must be out before the barrier
            // counts what was sent
            flush_all_write_batches_();

            finish_db_process();
        }

        // fixme start_db_process and finish_db_process not actually setup yet
        void finish_db_process() {
            // Wait for the next barrier
            set_state_(STAGE_PROCESSED);
            start_barrier_wait();
        }

        void end_barrier_stage_begin() {
            start_db_process();
        }
        void end_barrier_stage_processed() {
            set_state_(STAGE_CLOSING);
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
            // Report the finished close (every staged change is applied by
            // now) before the counters are reset by the next pass
            publish_progress();

            // Now all agents have closed their stages, so the pass is over
            end_pass_();

            // Every staged change has been applied and freed; give that memory
            // back so the next pass's budget sees it
            db_.release_free_memory();

            // We can start the loop again
            if (local_process_again_) {
                // this is mostly unnecessary, but it is nice to not set to
                // PRELOAD if we're going to process again so it's easier to
                // know that the entire mesh has not finished processing
                set_state_(STAGE_BEGIN);
            } else
                set_state_(PRELOAD);

            //if we are the synchronizer, and need to process again, let everyone know to do that
            if (has_ownership(sync_key)) {
                // if we are the sync agent and we get to closing, then we must have received sent distributions from everyone and confirmed that they match(?) recv distributions
                if (synchronizer_process_again_) {
                    synchronizer_process_again_ = false;
                    has_alg_processed_ = false;
                    broadcast_process(false);
                } else {
                    // move everyone into the alg db process
                    // Filter installs are broadcast to every agent, so the
                    // synchronizer's installed filters match everyone's
                    if (skip_group_filters_ || !db_.has_group_filters()) has_alg_processed_ = true;
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
            at_barrier_ = false;

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

        /** @brief Start a processing pass on every agent (and self)
         *
         * @param new_request true for the first pass of a process request
         *        from a client, false for another pass the synchronizer
         *        starts to continue the same request. Each request starts a
         *        new sweep over the entries (see SeqDB::start_new_sweep).
         */
        void broadcast_process(bool new_request = true) {
            char msg[sizeof(msg_type_t) + sizeof(uint8_t)];
            char* msg_ptr = msg;
            pack_msg(msg_ptr, PROCESS);
            pack_single(msg_ptr, (uint8_t)(new_request ? 1 : 0));
            pub(msg, sizeof(msg));

            //also send to self
            auto req = get_req(addr_.serialize());
            req->send(msg, sizeof(msg));
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
            set_state_(STAGE_BEGIN);

            broadcast_process(true);

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Call process on the internal DB */
        void recv_process(zmq_socket_t sock, const char* data, const char* end) {
            has_alg_processed_ = false;

            // The first pass of a client's request starts a new sweep. (A
            // PROCESS without the flag byte is treated as a new request.)
            bool new_request = (data == end) || (*data != 0);
            if (new_request) db_.start_new_sweep();

            // What this agent reports before its filters run: whether the last
            // stage close left changes staged. Applying an entry at a close
            // can release a subscription, and the release (a tag removal) is
            // only staged then, to be applied at this pass's close; the entry
            // it makes eligible needs one more pass after that. Otherwise
            // nothing is staged now, and the report must not repeat the last
            // pass's answer: that made every request end with two passes that
            // ran no filter instead of one.
            local_process_again_ = db_.has_staged_changes();

            // First, enter STAGE_BEGIN
            db_.stats().begin_pass(PASS_SINGLE);
            set_state_(STAGE_BEGIN);

            // Then, wait for a barrier
            start_barrier_wait();

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Start the internal alg db process mechanism */
        void recv_algprocess(zmq_socket_t sock, [[maybe_unused]] const char* data, [[maybe_unused]] const char* end) {
            db_.stats().begin_pass(PASS_GROUP);
            set_state_(STAGE_ALG_PROCESS);

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

        /* Applying writes this agent owns. Each write type keeps its own
         * rules for when it is applied directly and when it is staged until
         * the stage closes; single messages and WRITE_BATCH records share
         * these. The caller has checked ownership and counted the message
         * for the barrier. */

        void apply_add_entry_(DBEntry<> e) {
            if (state_ == STAGE_CLOSING || state_ == PRELOAD) {
                db_.add_entry_worker(move(e));
                if (state_ == STAGE_CLOSING) ++db_.stats().close.entries_done;
            }
            else db_.stage_add_entry_worker(move(e));
        }

        void apply_add_tag_(dbkey_t key, const string& tag) {
            // Only applied while closing the stage
            if (state_ == STAGE_CLOSING) {
                db_.add_tag_to_entry(key, tag);
                ++db_.stats().close.tags_added_done;
            }
            else db_.stage_add_tag_worker(key, tag);
        }

        void apply_remove_tag_(dbkey_t key, const string& tag) {
            if (state_ == STAGE_CLOSING || state_ == PRELOAD) {
                db_.remove_tag_from_entry(key, tag);
                if (state_ == STAGE_CLOSING) ++db_.stats().close.tags_removed_done;
            }
            else db_.stage_remove_tag_worker(key, tag);
        }

        void apply_update_val_(dbkey_t key, const string& val) {
            if (state_ == STAGE_CLOSING || state_ == PRELOAD) {
                db_.update_entry_val(key, val);
                if (state_ == STAGE_CLOSING) ++db_.stats().close.vals_done;
            }
            else db_.stage_update_entry_val_worker(key, val);
        }

        void apply_subscribe_(dbkey_t my_key, dbkey_t wait_key, const string& tag) {
            // The owner of wait_key removes the tag when that entry is created
            db_.subscribe_to_entry(my_key, wait_key, tag);
        }

        /** @brief Forward a message for a key this agent does not own to its
         * owner (only supported before processing starts) */
        void forward_to_owner_(dbkey_t key, const char* data_start, const char* end) {
            if (state_ != PRELOAD) throw runtime_error("Unimplemented");
            auto req = find_req(key);
            const char* full_msg = get_full_msg(data_start);
            req->send(full_msg, end - full_msg);
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
                apply_add_entry_(move(e));
            } else {
                if (state_ != PRELOAD) cout << "state was not preload, ERROR" << endl;
                forward_to_owner_(e.get_key(), data_start, end);
            }

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Add a tag to an existing entry, either here or forward */
        void recv_add_tag_to_entry(zmq_socket_t sock, const char* data, const char* end) {
            const char* data_start = data;
            dbkey_t key;
            unpack_single(data, key);
            string tag {data, end};

            if (has_ownership(key)) {
                update_recv_dist();
                apply_add_tag_(key, tag);
            } else forward_to_owner_(key, data_start, end);

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Remove a tag from an existing entry, either here or forward */
        void recv_remove_tag_from_entry(zmq_socket_t sock, const char* data, const char* end) {
            const char* data_start = data;
            dbkey_t key;
            unpack_single(data, key);
            string tag {data, end};

            if (has_ownership(key)) {
                update_recv_dist();
                apply_remove_tag_(key, tag);
            } else forward_to_owner_(key, data_start, end);

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Update the value of an existing entry, either here or forward */
        void recv_update_entry_val(zmq_socket_t sock, const char* data, const char* end) {
            const char* data_start = data;
            dbkey_t key;
            unpack_single(data, key);
            string val {data, end};

            if (has_ownership(key)) {
                update_recv_dist();
                apply_update_val_(key, val);
            } else forward_to_owner_(key, data_start, end);

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Subscribe to the creation of an entry, either here or forward */
        void recv_subscribe_to_entry(zmq_socket_t sock, const char* data, const char* end) {
            const char* data_start = data;
            dbkey_t my_key;
            dbkey_t wait_key;
            unpack_single(data, my_key);
            unpack_single(data, wait_key);
            string tag {data, end};

            //the owner of the wait key will be the one to trigger the tag
            //remove upon entry creation
            if (has_ownership(wait_key)) {
                update_recv_dist();
                apply_subscribe_(my_key, wait_key, tag);
            } else forward_to_owner_(wait_key, data_start, end);

            // If necessary, respond with an acknowledgement
            if (ZMQRequester::is_reqrep_sock(sock))
                ack(sock);
        }

        /** @brief Apply a WRITE_BATCH: several writes for keys this agent owns,
         * in the order they were made (see write_batches_)
         *
         * The batch counts as one received message for the barrier, as it
         * counted as one sent message. Every record must be for a key this
         * agent owns: batches are only sent while processing, when writes
         * cannot be forwarded.
         */
        void recv_write_batch(zmq_socket_t sock, const char* data, const char* end) {
            auto need = [&](size_t n) {
                if ((size_t)(end - data) < n) throw runtime_error("WRITE_BATCH: truncated message");
            };
            auto get_string = [&]() {
                size_t len;
                need(sizeof(size_t));
                unpack_single(data, len);
                need(len);
                string str {data, len};
                data += len;
                return str;
            };
            auto check_owner = [&](dbkey_t key) {
                if (!has_ownership(key)) throw runtime_error("WRITE_BATCH: write for a key this agent does not own");
            };

            size_t count;
            need(sizeof(size_t));
            unpack_single(data, count);

            for (size_t i = 0; i < count; ++i) {
                uint8_t op;
                need(sizeof(uint8_t));
                unpack_single(data, op);
                switch (op) {
                    case WRITE_OP_ADD_ENTRY: {
                        DBEntry<> e {data};
                        if (data > end) throw runtime_error("WRITE_BATCH: truncated entry");
                        auto k1 = e.get_key();
                        db_.verify_entry_key(&e);
                        if (k1 != e.get_key()) throw runtime_error("WRITE_BATCH: entry without a final key");
                        check_owner(e.get_key());
                        apply_add_entry_(move(e));
                        break;
                    }
                    case WRITE_OP_ADD_TAG:
                    case WRITE_OP_REMOVE_TAG:
                    case WRITE_OP_UPDATE_VAL: {
                        dbkey_t key;
                        need(sizeof(dbkey_t));
                        unpack_single(data, key);
                        string str = get_string();
                        check_owner(key);
                        if (op == WRITE_OP_ADD_TAG) apply_add_tag_(key, str);
                        else if (op == WRITE_OP_REMOVE_TAG) apply_remove_tag_(key, str);
                        else apply_update_val_(key, str);
                        break;
                    }
                    case WRITE_OP_SUBSCRIBE: {
                        dbkey_t my_key, wait_key;
                        need(2*sizeof(dbkey_t));
                        unpack_single(data, my_key);
                        unpack_single(data, wait_key);
                        string tag = get_string();
                        check_owner(wait_key);
                        apply_subscribe_(my_key, wait_key, tag);
                        break;
                    }
                    default:
                        throw runtime_error("WRITE_BATCH: unknown write type " + to_string((int)op));
                }
            }
            if (data != end) throw runtime_error("WRITE_BATCH: unexpected bytes after the last write");

            update_recv_dist();

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
            string ret = state_name(state_);

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
                // Random keys are not part of any group (the sequential
                // version and setup_graph_datastructures_have_ids skip them too)
                if (is_random_key(k)) continue;

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
void s_ref_get_entries_by_keys(fn_ref r, size_t n, const dbkey_t* keys, char** values) {
    ParDB* db = (ParDB*)r;
    db->get_entries_by_keys(n, keys, values);
}
}
