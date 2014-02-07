/*
 * ===============================================================
 *    Description:  Remote node pointer for edges.
 *
 *        Created:  03/01/2013 11:29:16 AM
 *
 *         Author:  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

#ifndef __REMOTE_NODE__
#define __REMOTE_NODE__

#include "node_prog/node_handle.h"

namespace db
{
namespace element
{
    class remote_node : public node_prog::node_handle
    {
        public:
            remote_node();
            remote_node(uint64_t, uint64_t);

        public:
            uint64_t loc;
            uint64_t id;
            uint64_t get_id();
            bool operator==(const db::element::remote_node &t) const;
            bool operator!=(const db::element::remote_node &t) const;
    };

    static db::element::remote_node coord_remote_node(0,0);
    static node_prog::node_handle& coordinator = (node_prog::node_handle &) coord_remote_node;
    
    inline
    remote_node :: remote_node(uint64_t l, uint64_t h)
        : loc(l)
        , id(h)
    { }

    inline remote_node :: remote_node() { }

    inline uint64_t 
    remote_node :: get_id()
    {
        return id;
    }

    inline bool
    remote_node :: operator==(const db::element::remote_node &t) const
    {
        return (id == t.id) && (loc == t.loc);
    }

    inline bool
    remote_node :: operator!=(const db::element::remote_node &t) const
    {
        return (id != t.id) || (loc != t.loc);
    }
    
}
}

namespace std
{
    // used if we want a hash table with a remote node as the key
    template <>
    struct hash<db::element::remote_node> 
    {
        public:
            size_t operator()(const db::element::remote_node &x) const throw() 
            {
                return (hash<int>()(x.loc) * 6291469) + (hash<size_t>()(x.id) * 393241); // some big primes
            }
    };

    // used if we want a hash table with a node_handle as the key
    template <>
    struct hash<node_prog::node_handle> 
    {
        public:
            size_t operator()(const node_prog::node_handle &x) const throw() 
            {
                const db::element::remote_node& toHash = dynamic_cast<const db::element::remote_node&>(x);
                return std::hash<db::element::remote_node>()(toHash);
            }
    };
}

#endif
