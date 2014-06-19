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

#ifndef weaver_common_nmap_stub_h_
#define weaver_common_nmap_stub_h_

#include <assert.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <hyperdex/client.hpp>
#include <hyperdex/datastructures.h>

namespace nmap
{
    class nmap_stub
    {
        public:
            nmap_stub();

        private:
            const char *space = "weaver_loc_mapping";
            const char *attrName = "shard"; // we get an attribute named "shard" with integer value correspoding to which shard node is placed on
            hyperdex::Client cl;

        public:
            bool put_mappings(std::unordered_map<uint64_t, uint64_t> &pairs_to_add);
            std::vector<std::pair<uint64_t, uint64_t>> get_mappings(std::unordered_set<uint64_t> &toGet);
            bool del_mappings(std::unordered_set<uint64_t> &toDel);
    };
}

#endif
