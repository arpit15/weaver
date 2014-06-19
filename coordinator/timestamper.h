/*
 * ===============================================================
 *    Description:  Coordinator vector timestamper class
 *                  definition. Vector timestampers receive client
 *                  requests, attach ordering related metadata,
 *                  and forward them to appropriate shards.
 *
 *        Created:  07/22/2013 12:23:33 PM
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

#ifndef weaver_coordinator_timestamper_h_
#define weaver_coordinator_timestamper_h_
#define VT_TIMEOUT_NANO 1000000 // number of nanoseconds between successive nops
#define VT_CLK_TIMEOUT_NANO 1000000 // number of nanoseconds between vt gossip

#include <vector>
#include <bitset>
#include <unordered_map>
#include <po6/threads/mutex.h>
#include <po6/threads/rwlock.h>
#include <po6/threads/cond.h>

#include "common/ids.h"
#include "common/vclock.h"
#include "common/message.h"
#include "common/transaction.h"
#include "common/configuration.h"
#include "common/comm_wrapper.h"
#include "common/server_manager_link_wrapper.h"
#include "common/nmap_stub.h"
#include "coordinator/current_tx.h"
#include "coordinator/current_prog.h"
#include "coordinator/hyper_stub.h"

namespace coordinator
{
    class timestamper
    {
        typedef std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>> req_queue_t;
        typedef std::unordered_map<uint64_t, std::bitset<NUM_SHARDS>> prog_reply_t;

        public:
            // messaging
            common::comm_wrapper comm;

            // server manager
            server_manager_link_wrapper sm_stub;
            configuration config;
            bool active_backup, first_config, vts_init;
            po6::threads::cond backup_cond, first_config_cond, vts_init_cond;
            
            // Hyperdex stub
            std::vector<hyper_stub*> hstub;

            // timestamper state
            uint64_t vt_id, shifted_id, id_gen; // this vector timestamper's id
            server_id server;
            uint64_t loc_gen;

            // consistency
            vc::vclock vclk; // vector clock
            vc::qtimestamp_t qts; // queue timestamp
            uint64_t clock_update_acks, clk_updates;
            std::bitset<NUM_SHARDS> to_nop;
            uint64_t nop_ack_qts[NUM_SHARDS];

            // write transactions
            std::unordered_map<uint64_t, current_tx> outstanding_tx;
            std::unordered_map<uint64_t, current_tx> del_tx;
            po6::threads::mutex busy_mtx;
            std::unordered_map<uint64_t, uint64_t> deleted_elems; // del_elem -> tx that locked it
            std::unordered_map<uint64_t, uint64_t> busy_elems; // busy_elem -> num of busy locks

            // node prog
            std::unordered_map<uint64_t, current_prog> outstanding_progs;

            // prog cleanup and permanent deletion
            req_queue_t pend_prog_queue;
            req_queue_t done_prog_queue;
            uint64_t max_done_id; // permanent deletion of migrated nodes
            std::unique_ptr<vc::vclock_t> max_done_clk; // permanent deletion
            std::unordered_map<node_prog::prog_type, prog_reply_t> done_reqs; // prog state cleanup

            // node map client
            std::vector<nmap::nmap_stub*> nmap_client;

            // mutexes
        public:
            po6::threads::mutex clk_mutex // vclock and queue timestamp
                    , loc_gen_mutex
                    , tx_prog_mutex // state for outstanding and completed node progs, transactions
                    , periodic_update_mutex // make sure to not send out clock update before getting ack from other VTs
                    , id_gen_mutex
                    , msg_count_mutex
                    , migr_mutex
                    , graph_load_mutex
                    , config_mutex
                    , exit_mutex;
            po6::threads::rwlock clk_rw_mtx;

            // initial graph loading
            uint32_t load_count;
            uint64_t max_load_time;

            // migration
            uint64_t migr_client;
            std::vector<uint64_t> shard_node_count;
            uint64_t msg_count, msg_count_acks;

            // exit
            bool to_exit;

        public:
            timestamper(uint64_t vt, uint64_t server);
            void init();
            void restore_backup();
            void reconfigure();
            uint64_t generate_id();
    };

    inline
    timestamper :: timestamper(uint64_t vtid, uint64_t serverid)
        : comm(serverid, NUM_THREADS, -1, false)
        , sm_stub(server_id(serverid), comm.get_loc())
        , active_backup(false)
        , first_config(false)
        , vts_init(false)
        , backup_cond(&config_mutex)
        , first_config_cond(&config_mutex)
        , vts_init_cond(&config_mutex)
        , vt_id(vtid)
        , shifted_id(vtid << (64-ID_BITS))
        , id_gen(0)
        , server(serverid)
        , loc_gen(0)
        , vclk(vtid, 0)
        , qts(NUM_SHARDS, 0)
        , clock_update_acks(NUM_VTS-1)
        , clk_updates(0)
        , max_done_id(0)
        , max_done_clk(new vc::vclock_t(NUM_VTS, 0))
        , load_count(0)
        , max_load_time(0)
        , shard_node_count(NUM_SHARDS, 0)
        , msg_count(0)
        , msg_count_acks(0)
        , to_exit(false)
    {
        // initialize empty vector of done reqs for each prog type
        std::unordered_map<uint64_t, std::bitset<NUM_SHARDS>> empty_map;
        done_reqs.emplace(node_prog::REACHABILITY, empty_map);
        done_reqs.emplace(node_prog::DIJKSTRA, empty_map);
        done_reqs.emplace(node_prog::CLUSTERING, empty_map);
        for (int i = 0; i < NUM_THREADS; i++) {
            nmap_client.push_back(new nmap::nmap_stub());
        }
        to_nop.set(); // set to_nop to 1 for each shard
        for (int i = 0; i < NUM_SHARDS; i++) {
            nop_ack_qts[i] = 0;
        }
        for (int i = 0; i < NUM_THREADS; i++) {
            hstub.push_back(new hyper_stub(vt_id));
        }
    }

    // initialize msging layer
    // caution: holding config_mutex
    inline void
    timestamper :: init()
    {
        comm.init(config);
    }

    // restore state when backup becomes primary due to failure
    inline void
    timestamper :: restore_backup()
    {
        std::unordered_map<uint64_t, current_tx> prepare_txs;
        hstub.back()->restore_backup(prepare_txs, outstanding_tx);
        WDEBUG << "num prep " << prepare_txs.size()
               << ", outst txs " << outstanding_tx.size() << std::endl;
        // TODO restore tx state
    }

    // reconfigure timestamper according to new cluster configuration
    inline void
    timestamper :: reconfigure()
    {
        WDEBUG << "Cluster reconfigure triggered\n";

        // print cluster info
        for (uint64_t i = 0; i < NUM_SERVERS; i++) {
            server::state_t st = config.get_state(server_id(i));
            if (st != server::AVAILABLE) {
                WDEBUG << "Server " << i << " is in trouble, has state " << st << std::endl;
            } else {
                WDEBUG << "Server " << i << " is healthy, has state " << st << std::endl;
            }
        }

        if (comm.reconfigure(config) == server.get()) {
            // this server is now primary for the shard
            active_backup = true;
            backup_cond.signal();
        }

        // resend unacked transactions and nop for new shard
        // TODO flesh out
        //uint64_t max_qts = 0;
        //bool pending_tx = false;
        //if (changed >= SHARD_ID_INCR && changed < (SHARD_ID_INCR+NUM_SHARDS)) {
        //    // transactions
        //    uint64_t sid = changed - SHARD_ID_INCR;
        //    for (auto &entry: outstanding_tx) {
        //        std::vector<transaction::pending_tx> &tv = *entry.second.tx_vec;
        //        if (!tv[sid].writes.empty()) {
        //            pending_tx = true;
        //            // TODO resend transactions
        //            if (tv[sid].writes.back()->qts[sid] > max_qts) {
        //                max_qts = tv[sid].writes.back()->qts[sid];
        //            }
        //        }
        //    }
        //    assert(qts[sid] >= max_qts);

        //    // nop
        //    if (!to_nop[sid]) {
        //        nop_ack_qts[sid] = qts[sid]; // artificially 'ack' old nop
        //        to_nop.set(sid);
        //        WDEBUG << "resetting to_nop for shard " << changed << std::endl;
        //    }

        //    if (!pending_tx) {
        //        // only nop was pending, if any
        //        // send msg to advance clock
        //        message::message msg;
        //        msg.prepare_message(message::SET_QTS, vt_id, qts[sid]);
        //        comm.send(changed, msg.buf);
        //        WDEBUG << "sent set qts msg to shard " << changed << std::endl;
        //    }
        //}
    }

    inline uint64_t
    timestamper :: generate_id()
    {
        uint64_t new_id;
        id_gen_mutex.lock();
        new_id = (++id_gen) & TOP_MASK;
        id_gen_mutex.unlock();
        new_id |= shifted_id;
        return new_id;
    }

}

#endif
