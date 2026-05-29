//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_HASH_H
#define WILL_ENGINE_HASH_H

#include <cstdint>
#include <cstring>

#include "core/hash/fnv_1_a.h"
#include "core/string_id.h"
#include "inline_path.h"

namespace Core
{
template<typename K>
struct Hash;

template<> struct Hash<uint32_t>    { uint64_t operator()(uint32_t k)    const { return static_cast<uint64_t>(k); } };
template<> struct Hash<uint64_t>    { uint64_t operator()(uint64_t k)    const { return k; } };
template<> struct Hash<int32_t>     { uint64_t operator()(int32_t k)     const { return static_cast<uint64_t>(k); } };
template<> struct Hash<int64_t>     { uint64_t operator()(int64_t k)     const { return static_cast<uint64_t>(k); } };
template<> struct Hash<StringID>    { uint64_t operator()(StringID k)    const { return k.id; } };
template<> struct Hash<const char*>
{
    uint64_t operator()(const char* k) const
    {
        return k ? fnv1a64(k, strlen(k)) : 0;
    }
};
template<size_t N> struct Hash<InlinePath<N>>
{
    uint64_t operator()(const InlinePath<N>& k) const
    {
        return fnv1a64(k.c_str(), k.Size());
    }
};
} // namespace Core

#endif // WILL_ENGINE_HASH_H
