#pragma once

#include "alg_access.hpp"

namespace pando {
    /**
     * @tparam N the value type of the neighbor
     * @tparam T the value type of the group / vertex
     */
    template<class N, class T>
    class GASFilter {
        private:
            DBAccess* acc_;
            GroupAccess& g_;
            T val_;
            bool initialized_ = false;

        protected:
            /** @brief the current vertex */
            vtx_t v;

            /** @brief the current iteration */
            size_t& iter;

            /** @brief the gather function
             *
             * Gather will be run on each "edge" and will produce a single
             * value from all of the input edge values.
             *
             * @param a an edge value or a partially reduced edge value
             * @param b an edge value or a partially reduced edge value
             * @return a value that is the pair-wise reduction (gather) of a and b
             */
            virtual N G(N a, N b) = 0;

            /** @brief the apply function
             *
             * This converts the previously gathered value into a new value for
             * the entry, after all gathering has finished.  It will use the
             * old value from the entry and the newly gathered value.
             *
             * @param v the old value
             * @param a the newly gathered value
             * @return the new value to set
             */
            virtual T A(T v, N a) = 0;

            /** @brief the scatter function
             *
             * This creates new neighbor values for send out to each neighbor
             * @param v the current value
             * @return the new value to send to neighbors
             */
            virtual N S(T v) = 0;

            /** @brief initial value to use to gather */
            virtual N gather_init(DBAccess* acc) = 0;

            /** @brief Function that will get the value of the GAS entry
             * Also creates the entry if it does not exist.
            */
            virtual T init([[maybe_unused]] DBAccess* acc) { throw runtime_error("Invalid state"); }

            /** @brief Function that will set the value of the GAS entry */
            virtual void save_output(DBAccess* acc, N val) = 0;

            /** @brief Check if the values of this type are close (or equal) */
            virtual bool isclose(T a, T b) const {
                return a == b;
            }
        public:
            /** @brief Initialize a new gas filter with the given access */
            GASFilter(DBAccess* access) : acc_(access), g_(*(acc_->group)), iter(g_.iter) { }
            virtual ~GASFilter() = default;

            void finish() {
                save_output(acc_, val_);
            }

            /** @brief Run the GAS filter */
            void run() {
                if (!initialized_) {
                    val_ = init(acc_);
                    initialized_ = true;
                }
                auto &in_msgs = *(g_.in_msgs);
                auto &out_msgs = *(g_.out_msgs);
                v = acc_->key.b;

                // Note: the 0 iteration is a special iteration that does NOT
                // gather from neighbors, but instead just uses the init val,
                // which is the initial result from after "applying"

                if (g_.iter > 0) {
                    // Gather phase
                    N gather_val = gather_init(acc_);

                    for (auto &msg : in_msgs[g_.iter][v]) {
                        N cand_gather_val = msg.getval<N>();
                        gather_val = G(cand_gather_val, gather_val);
                    }

                    // Apply phase
                    auto old_val = val_;
                    val_ = A(gather_val, val_);

                    // Become ACTIVE if we changed values, otherwise go inactive
                    if (isclose(old_val, val_)) {
                        // We know we *may* finish running with this as the last run,
                        // so save off the current state
                        finish();
                        g_.state = INACTIVE;
                    } else
                        g_.state = ACTIVE;
                    // ACTIVE will be used by the alg db in the following way:
                    // if everyone is INACTIVE, then terminate;
                    // otherwise, if there is one or more ACTIVE across the entire
                    // (parallel) DB, then continue execution
                } else
                    g_.state = ACTIVE;

                // Scatter phase: only on the first iteration or when the value
                // changed. An unchanged vertex has nothing new to tell its
                // neighbors (they already have its last value), and the AlgDB
                // keeps iterating while any message is pending, so scattering
                // unconditionally would never terminate.
                if (g_.iter > 0 && g_.state == INACTIVE)
                    return;

                N sval = S(val_);
                for (auto &out_edge: g_.entry.out) {
                    MData md {sval};

                    // Scatter at the next iteration
                    out_msgs[g_.iter+1][out_edge.n].push_back(md);
                }

                // Promise from infrastructure:
                // All messages at g_.iter will be sent and received before any
                // computation happens again
            }
    };
}
            /*
  How to emulate GAS in this "local model":
  we have: g(..), a(..), and s(..)

  (filter does the following):
  init(v):
    v.it = 0
    v.val = init()

  f(v):
    // all RECEIVED messages are in "msg", format: msg[src][dst]["key"], where "key" is normally iteration
    // we have functions: g, a, s
    // needs to return vA and vB

    // GATHER phase
    if (v.it > 0)
        for (n : neighbors)
            v.val = g(v.val, msg[n][v][it])

    // APPLY phase
    a(v.val)

    v.it += 1

    // SCATTER phase
    sval = s(v)
    for (n : neighbors)
        msg[v][n][v.it] = sval

    v.notify_neighbors = true
    v.active = true
    v.at_barrier = true     // says: don't progress until every other group entry being processed is also "at barrier" (within the same graph)
    // require messages from all neighbors
    for (n : neighbors)
        //              src, dst, "key"
        req_msg.insert((n,   v,   v.it))

    */
