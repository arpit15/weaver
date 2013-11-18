/*
 * ===============================================================
 *    Description:  Wrapper around HyperDex client for getting and
 *                  putting coordinator state-related mappings.
 *
 *        Created:  09/05/2013 11:18:57 AM
 *
 *         Author:  Greg Hill, gdh39@cornell.edu
 *                  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

#include <vector>
#include <hyperdex/client.hpp>
#include <hyperdex/datastructures.h>
#include <po6/threads/mutex.h>

#include "common/weaver_constants.h"

namespace nmap
{
    class nmap_stub
    {
        public:
            nmap_stub();

        private:
            const char *loc_space = "weaver_loc_mapping";
            const char *edge_space = "weaver_edge_mapping";
            const char *loc_attrName = "shard"; // we get an attribute named "shard" with integer value correspoding to which shard node is placed on
            const char *edge_attrName = "node"; // we get an attribute named "node" with integer value which tells us the source vertex of the edge
            hyperdex::Client cl;

        public:
            void put_mappings(std::unordered_map<uint64_t, uint64_t> &pairs_to_add, bool space);
            std::vector<std::pair<uint64_t, uint64_t>> get_mappings(std::unordered_set<uint64_t> &toGet, bool space);
            void del_mappings(std::vector<uint64_t> &toDel, bool space);
            void clean_up_space();
    };

    inline
    nmap_stub :: nmap_stub() : cl(HYPERDEX_COORD_IPADDR, HYPERDEX_COORD_PORT) { }

    //inline bool
    //nmap_stub :: put_single_mapping(uint64_t key, uint64_t value, hyperdex_client_attribute &attrib,
    //        const char *space, const char *attrName)
    //{
    //    attrib.attr = attrName;
    //    attrib.value = (char*)&value;
    //    attrib.value_sz = sizeof(int64_t);
    //    attrib.datatype = HYPERDATATYPE_INT64;
    //    
    //    hyperdex_client_returncode put_status;
    //    int64_t op_id = cl.put(space, (const char *)&key, sizeof(int64_t), &attrib, 1, &put_status);
    //    if (op_id < 0) {
    //        WDEBUG << "\"put\" returned " << op_id << " with status " << put_status << std::endl;
    //        return false;
    //    }
    //    return true;
    //}
    
    // sel_space: true = node map, false = edge_map
    inline void
    nmap_stub :: put_mappings(std::unordered_map<uint64_t, uint64_t> &pairs_to_add, bool sel_space)
    {
        const char *space = sel_space ? loc_space : edge_space;
        const char *attrName = sel_space ? loc_attrName : edge_attrName;
        int numPairs = pairs_to_add.size();
        int i;
        hyperdex_client_attribute *attrs_to_add = (hyperdex_client_attribute *) malloc(numPairs * sizeof(hyperdex_client_attribute));

        i = 0;
        for (auto &entry: pairs_to_add) {
            attrs_to_add[i].attr = attrName;
            attrs_to_add[i].value = (char *) &entry.second;
            attrs_to_add[i].value_sz = sizeof(int64_t);
            attrs_to_add[i].datatype = HYPERDATATYPE_INT64;

            hyperdex_client_returncode put_status;
            int64_t op_id = cl.put(space, (const char *) &entry.first, sizeof(int64_t), &(attrs_to_add[i]), 1, &put_status);
            if (op_id < 0) {
                WDEBUG << "\"put\" returned " << op_id << " with status " << put_status << std::endl;
                free(attrs_to_add);
                return;
            }
            i++;
        }

        hyperdex_client_returncode loop_status;
        int64_t loop_id;
        // call loop once for every put
        for (i = 0; i < numPairs; i++) {
            loop_id = cl.loop(-1, &loop_status);
            if (loop_id < 0) {
                WDEBUG << "put \"loop\" returned " << loop_id << " with status " << loop_status << std::endl;
                free(attrs_to_add);
                return;
            }
        }
        free(attrs_to_add);
    }

    std::vector<std::pair<uint64_t, uint64_t>>
    nmap_stub :: get_mappings(std::unordered_set<uint64_t> &toGet, bool sel_space)
    {
        const char *space = sel_space ? loc_space : edge_space;
        int numNodes = toGet.size();
        struct async_get {
            uint64_t key;
            int64_t op_id;
            hyperdex_client_returncode get_status;
            const hyperdex_client_attribute * attr;
            size_t attr_size;
        };
        std::vector<struct async_get > results(numNodes);
        auto nextHandle = toGet.begin();
        for (int i = 0; i < numNodes; i++) {
            results[i].key = *nextHandle;
            nextHandle++;
        }

        for (int i = 0; i < numNodes; i++) {
            results[i].op_id = cl.get(space, (char *) &(results[i].key), sizeof(uint64_t), 
                &(results[i].get_status), &(results[i].attr), &(results[i].attr_size));
        }

        hyperdex_client_returncode loop_status;
        int64_t loop_id;
        // call loop once for every get
        for (int i = 0; i < numNodes; i++) {
            loop_id = cl.loop(-1, &loop_status);
            if (loop_id < 0) {
                WDEBUG << "get \"loop\" returned " << loop_id << " with status " << loop_status << std::endl;
                std::vector<std::pair<uint64_t, uint64_t>> empty(0);
                return empty; // free previously gotten attrs
            }

            // double check this ID exists
            bool found = false;
            for (int i = 0; i < numNodes; i++) {
                found = found || (results[i].op_id == loop_id);
            }
            assert(found);
        }

        std::vector<std::pair<uint64_t, uint64_t>> toRet;//numNodes);
        for (int i = 0; i < numNodes; i++) {
            if (results[i].attr_size == 0) {
                WDEBUG << "Key " << results[i].key << " did not exist in hyperdex" << std::endl;
            } else if (results[i].attr_size > 1) {
                WDEBUG << "\"get\" number " << i << " returned " << results[i].attr_size << " attrs" << std::endl;
            } else {
                uint64_t* shard = (uint64_t *) results[i].attr->value;
                uint64_t nodeID = results[i].key;
                toRet.emplace_back(nodeID, *shard);
            }
            hyperdex_client_destroy_attrs(results[i].attr, results[i].attr_size);
        }
        return toRet;
    }

    void
    nmap_stub :: del_mappings(std::vector<uint64_t> &toDel, bool sel_space)
    {
        const char *space = sel_space ? loc_space : edge_space;
        int numNodes = toDel.size();
        std::vector<int64_t> results(numNodes);
        hyperdex_client_returncode get_status;

        for (int i = 0; i < numNodes; i++) {
            results[i] = cl.del(space, (char *) &(toDel[i]), sizeof(uint64_t), &get_status);
            if (results[i] < 0)
            {
                WDEBUG << "\"del\" returned " << results[i] << " with status " << get_status << std::endl;
                return;
            }
        }

        hyperdex_client_returncode loop_status;
        int64_t loop_id;
        // call loop once for every get
        for (int i = 0; i < numNodes; i++) {
            loop_id = cl.loop(-1, &loop_status);
            if (loop_id < 0) {
                WDEBUG << "get \"loop\" returned " << loop_id << " with status " << loop_status << std::endl;
                return;
            }

            // double check this ID exists
            bool found = false;
            for (int i = 0; i < numNodes; i++) {
                found = found || (results[i] == loop_id);
            }
            assert(found);
        }
    }

    // TODO
    inline void
    nmap_stub :: clean_up_space()
    {
        //cl.rm_space(space);
    }

}