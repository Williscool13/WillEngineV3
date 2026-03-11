//
// Created by William on 2026-03-08.
//

#ifndef WILL_ENGINE_FNV_1_A_H
#define WILL_ENGINE_FNV_1_A_H
#include <cstdint>

constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME  = 1099511628211ULL;

constexpr uint64_t fnv1a64(const char* str, size_t len)
{
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(str[i]));
        hash *= FNV_PRIME;
    }
    return hash;
}

constexpr uint64_t fnv1a64(const uint8_t* str, size_t len)
{
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(str[i]);
        hash *= FNV_PRIME;
    }
    return hash;
}

constexpr uint64_t fnv1a64(const uint8_t* str, size_t len, uint64_t seed)
{
    uint64_t hash = seed;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(str[i]);
        hash *= FNV_PRIME;
    }
    return hash;
}

#endif //WILL_ENGINE_FNV_1_A_H