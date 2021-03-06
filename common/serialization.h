/*
 * ===============================================================
 *    Description:  Interfaces for po6 classes packers and
 *                  unpackers.
 *
 *        Created:  2014-02-08 17:26:46
 *
 *         Author:  Robert Escriva, escriva@cs.cornell.edu
 *                  Ayush Dubey, dubey@cs.cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

// Most of the following code has been 'borrowed' from
// Robert Escriva's HyperDex coordinator.
// see https://github.com/rescrv/HyperDex for the original code.

#ifndef weaver_common_serialization_h_
#define weaver_common_serialization_h_

// po6
#include <po6/net/location.h>
#include <po6/net/hostname.h>

// e
#include <e/buffer.h>

e::buffer::packer
operator << (e::buffer::packer lhs, const po6::net::ipaddr& rhs);
e::unpacker
operator >> (e::unpacker lhs, po6::net::ipaddr& rhs);
size_t
pack_size(const po6::net::ipaddr& rhs);

e::buffer::packer
operator << (e::buffer::packer lhs, const po6::net::location& rhs);
e::unpacker
operator >> (e::unpacker lhs, po6::net::location& rhs);
size_t
pack_size(const po6::net::location& rhs);

e::buffer::packer
operator << (e::buffer::packer lhs, const po6::net::hostname& rhs);
e::unpacker
operator >> (e::unpacker lhs, po6::net::hostname& rhs);
size_t
pack_size(const po6::net::hostname& rhs);

inline size_t
pack_size(uint64_t) { return sizeof(uint64_t); }

size_t
pack_size(const e::slice& s);

template <typename T>
size_t
pack_size(const std::vector<T>& v)
{
    size_t sz = sizeof(uint32_t);

    for (size_t i = 0; i < v.size(); ++i)
    {
        sz += pack_size(v[i]);
    }

    return sz;
}

#endif // weaver_common_serialization_h_
