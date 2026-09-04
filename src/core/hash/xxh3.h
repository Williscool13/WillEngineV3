//
// Created by William on 2026-09-04.
//

#ifndef WILL_ENGINE_XXH3_H
#define WILL_ENGINE_XXH3_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace XXH3Detail
{
constexpr uint8_t SECRET[192] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

constexpr uint64_t PRIME32_1 = 0x9E3779B1ULL;
constexpr uint64_t PRIME32_2 = 0x85EBCA77ULL;
constexpr uint64_t PRIME32_3 = 0xC2B2AE3DULL;
constexpr uint64_t PRIME64_1 = 0x9E3779B185EBCA87ULL;
constexpr uint64_t PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;
constexpr uint64_t PRIME64_3 = 0x165667B19E3779F9ULL;
constexpr uint64_t PRIME64_4 = 0x85EBCA77C2B2AE63ULL;
constexpr uint64_t PRIME64_5 = 0x27D4EB2F165667C5ULL;
constexpr uint64_t PRIME_MX1 = 0x165667919E3779F9ULL;
constexpr uint64_t PRIME_MX2 = 0x9FB21C651E98DF25ULL;

constexpr size_t STRIPE_LEN = 64;
constexpr size_t SECRET_CONSUME_RATE = 8;
constexpr size_t ACC_NB = 8;
constexpr size_t SECRET_SIZE = sizeof(SECRET);
constexpr size_t SECRET_SIZE_MIN = 136;
constexpr size_t MIDSIZE_MAX = 240;
constexpr size_t MIDSIZE_STARTOFFSET = 3;
constexpr size_t MIDSIZE_LASTOFFSET = 17;
constexpr size_t SECRET_LASTACC_START = 7;
constexpr size_t SECRET_MERGEACCS_START = 11;

template<typename B>
constexpr uint64_t Read64(const B* p)
{
    if (std::is_constant_evaluated()) {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(static_cast<uint8_t>(p[i])) << (8 * i);
        }
        return v;
    }
    uint64_t v = 0;
    memcpy(&v, p, 8);
    return v;
}

template<typename B>
constexpr uint32_t Read32(const B* p)
{
    if (std::is_constant_evaluated()) {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<uint32_t>(static_cast<uint8_t>(p[i])) << (8 * i);
        }
        return v;
    }
    uint32_t v = 0;
    memcpy(&v, p, 4);
    return v;
}

constexpr uint64_t Swap64(uint64_t x)
{
    return ((x << 56) & 0xff00000000000000ULL) | ((x << 40) & 0x00ff000000000000ULL) |
           ((x << 24) & 0x0000ff0000000000ULL) | ((x << 8) & 0x000000ff00000000ULL) |
           ((x >> 8) & 0x00000000ff000000ULL) | ((x >> 24) & 0x0000000000ff0000ULL) |
           ((x >> 40) & 0x000000000000ff00ULL) | ((x >> 56) & 0x00000000000000ffULL);
}

constexpr uint64_t Rotl64(uint64_t x, int r)
{
    return (x << r) | (x >> (64 - r));
}

constexpr uint64_t XorShift64(uint64_t v, int shift)
{
    return v ^ (v >> shift);
}

constexpr uint64_t Mul128Fold64(uint64_t lhs, uint64_t rhs)
{
    if (std::is_constant_evaluated()) {
        const uint64_t loLo = (lhs & 0xFFFFFFFFULL) * (rhs & 0xFFFFFFFFULL);
        const uint64_t hiLo = (lhs >> 32) * (rhs & 0xFFFFFFFFULL);
        const uint64_t loHi = (lhs & 0xFFFFFFFFULL) * (rhs >> 32);
        const uint64_t hiHi = (lhs >> 32) * (rhs >> 32);
        const uint64_t cross = (loLo >> 32) + (hiLo & 0xFFFFFFFFULL) + loHi;
        const uint64_t upper = (hiLo >> 32) + (cross >> 32) + hiHi;
        const uint64_t lower = (cross << 32) | (loLo & 0xFFFFFFFFULL);
        return lower ^ upper;
    }
#if defined(_MSC_VER) && !defined(__clang__)
    uint64_t hi = 0;
    const uint64_t lo = _umul128(lhs, rhs, &hi);
    return lo ^ hi;
#else
    const unsigned __int128 product = static_cast<unsigned __int128>(lhs) * rhs;
    return static_cast<uint64_t>(product) ^ static_cast<uint64_t>(product >> 64);
#endif
}

constexpr uint64_t Avalanche(uint64_t h)
{
    h = XorShift64(h, 37);
    h *= PRIME_MX1;
    return XorShift64(h, 32);
}

constexpr uint64_t Avalanche64(uint64_t h)
{
    h ^= h >> 33;
    h *= PRIME64_2;
    h ^= h >> 29;
    h *= PRIME64_3;
    h ^= h >> 32;
    return h;
}

constexpr uint64_t Rrmxmx(uint64_t h, uint64_t len)
{
    h ^= Rotl64(h, 49) ^ Rotl64(h, 24);
    h *= PRIME_MX2;
    h ^= (h >> 35) + len;
    h *= PRIME_MX2;
    return XorShift64(h, 28);
}

template<typename B>
constexpr uint64_t Len1To3(const B* input, size_t len)
{
    const uint32_t c1 = static_cast<uint8_t>(input[0]);
    const uint32_t c2 = static_cast<uint8_t>(input[len >> 1]);
    const uint32_t c3 = static_cast<uint8_t>(input[len - 1]);
    const uint32_t combined = (c1 << 16) | (c2 << 24) | c3 | (static_cast<uint32_t>(len) << 8);
    const uint64_t bitflip = Read32(SECRET) ^ Read32(SECRET + 4);
    return Avalanche64(static_cast<uint64_t>(combined) ^ bitflip);
}

template<typename B>
constexpr uint64_t Len4To8(const B* input, size_t len)
{
    const uint32_t input1 = Read32(input);
    const uint32_t input2 = Read32(input + len - 4);
    const uint64_t bitflip = Read64(SECRET + 8) ^ Read64(SECRET + 16);
    const uint64_t input64 = input2 + (static_cast<uint64_t>(input1) << 32);
    return Rrmxmx(input64 ^ bitflip, len);
}

template<typename B>
constexpr uint64_t Len9To16(const B* input, size_t len)
{
    const uint64_t bitflip1 = Read64(SECRET + 24) ^ Read64(SECRET + 32);
    const uint64_t bitflip2 = Read64(SECRET + 40) ^ Read64(SECRET + 48);
    const uint64_t inputLo = Read64(input) ^ bitflip1;
    const uint64_t inputHi = Read64(input + len - 8) ^ bitflip2;
    const uint64_t acc = len + Swap64(inputLo) + inputHi + Mul128Fold64(inputLo, inputHi);
    return Avalanche(acc);
}

template<typename B>
constexpr uint64_t Len0To16(const B* input, size_t len)
{
    if (len > 8) { return Len9To16(input, len); }
    if (len >= 4) { return Len4To8(input, len); }
    if (len > 0) { return Len1To3(input, len); }
    return Avalanche64(Read64(SECRET + 56) ^ Read64(SECRET + 64));
}

template<typename B>
constexpr uint64_t Mix16B(const B* input, const uint8_t* secret)
{
    const uint64_t inputLo = Read64(input);
    const uint64_t inputHi = Read64(input + 8);
    return Mul128Fold64(inputLo ^ Read64(secret), inputHi ^ Read64(secret + 8));
}

template<typename B>
constexpr uint64_t Len17To128(const B* input, size_t len)
{
    uint64_t acc = len * PRIME64_1;
    if (len > 32) {
        if (len > 64) {
            if (len > 96) {
                acc += Mix16B(input + 48, SECRET + 96);
                acc += Mix16B(input + len - 64, SECRET + 112);
            }
            acc += Mix16B(input + 32, SECRET + 64);
            acc += Mix16B(input + len - 48, SECRET + 80);
        }
        acc += Mix16B(input + 16, SECRET + 32);
        acc += Mix16B(input + len - 32, SECRET + 48);
    }
    acc += Mix16B(input, SECRET);
    acc += Mix16B(input + len - 16, SECRET + 16);
    return Avalanche(acc);
}

template<typename B>
constexpr uint64_t Len129To240(const B* input, size_t len)
{
    uint64_t acc = len * PRIME64_1;
    const size_t nbRounds = len / 16;
    for (size_t i = 0; i < 8; ++i) {
        acc += Mix16B(input + 16 * i, SECRET + 16 * i);
    }
    uint64_t accEnd = Mix16B(input + len - 16, SECRET + SECRET_SIZE_MIN - MIDSIZE_LASTOFFSET);
    acc = Avalanche(acc);
    for (size_t i = 8; i < nbRounds; ++i) {
        accEnd += Mix16B(input + 16 * i, SECRET + 16 * (i - 8) + MIDSIZE_STARTOFFSET);
    }
    return Avalanche(acc + accEnd);
}

template<typename B>
constexpr void Accumulate512(uint64_t* acc, const B* input, const uint8_t* secret)
{
    for (size_t i = 0; i < ACC_NB; ++i) {
        const uint64_t dataVal = Read64(input + 8 * i);
        const uint64_t dataKey = dataVal ^ Read64(secret + 8 * i);
        acc[i ^ 1] += dataVal;
        acc[i] += (dataKey & 0xFFFFFFFFULL) * (dataKey >> 32);
    }
}

template<typename B>
constexpr void Accumulate(uint64_t* acc, const B* input, const uint8_t* secret, size_t nbStripes)
{
    for (size_t n = 0; n < nbStripes; ++n) {
        Accumulate512(acc, input + n * STRIPE_LEN, secret + n * SECRET_CONSUME_RATE);
    }
}

constexpr void ScrambleAcc(uint64_t* acc, const uint8_t* secret)
{
    for (size_t i = 0; i < ACC_NB; ++i) {
        const uint64_t key64 = Read64(secret + 8 * i);
        uint64_t acc64 = acc[i];
        acc64 = XorShift64(acc64, 47);
        acc64 ^= key64;
        acc64 *= PRIME32_1;
        acc[i] = acc64;
    }
}

constexpr uint64_t Mix2Accs(const uint64_t* acc, const uint8_t* secret)
{
    return Mul128Fold64(acc[0] ^ Read64(secret), acc[1] ^ Read64(secret + 8));
}

constexpr uint64_t MergeAccs(const uint64_t* acc, const uint8_t* secret, uint64_t start)
{
    uint64_t result = start;
    for (size_t i = 0; i < 4; ++i) {
        result += Mix2Accs(acc + 2 * i, secret + 16 * i);
    }
    return Avalanche(result);
}

template<typename B>
constexpr uint64_t HashLong(const B* input, size_t len)
{
    uint64_t acc[ACC_NB] = {PRIME32_3, PRIME64_1, PRIME64_2, PRIME64_3, PRIME64_4, PRIME32_2, PRIME64_5, PRIME32_1};
    constexpr size_t NB_STRIPES_PER_BLOCK = (SECRET_SIZE - STRIPE_LEN) / SECRET_CONSUME_RATE;
    constexpr size_t BLOCK_LEN = STRIPE_LEN * NB_STRIPES_PER_BLOCK;
    const size_t nbBlocks = (len - 1) / BLOCK_LEN;

    for (size_t n = 0; n < nbBlocks; ++n) {
        Accumulate(acc, input + n * BLOCK_LEN, SECRET, NB_STRIPES_PER_BLOCK);
        ScrambleAcc(acc, SECRET + SECRET_SIZE - STRIPE_LEN);
    }

    const size_t nbStripes = ((len - 1) - (BLOCK_LEN * nbBlocks)) / STRIPE_LEN;
    Accumulate(acc, input + nbBlocks * BLOCK_LEN, SECRET, nbStripes);
    Accumulate512(acc, input + len - STRIPE_LEN, SECRET + SECRET_SIZE - STRIPE_LEN - SECRET_LASTACC_START);

    return MergeAccs(acc, SECRET + SECRET_MERGEACCS_START, len * PRIME64_1);
}

template<typename B>
constexpr uint64_t Hash64(const B* input, size_t len)
{
    if (len <= 16) { return Len0To16(input, len); }
    if (len <= 128) { return Len17To128(input, len); }
    if (len <= MIDSIZE_MAX) { return Len129To240(input, len); }
    return HashLong(input, len);
}
} // namespace XXH3Detail

constexpr uint64_t Hash(const char* data, size_t len)
{
    return XXH3Detail::Hash64(data, len);
}

constexpr uint64_t Hash(const uint8_t* data, size_t len)
{
    return XXH3Detail::Hash64(data, len);
}

inline uint64_t Hash(const void* data, size_t len)
{
    return XXH3Detail::Hash64(static_cast<const uint8_t*>(data), len);
}

/**
 * Order-dependent hash over several chunks. Each Add folds Hash(chunk) into the running state, so the result is a pure function of the sequence of chunks.
 */
struct HashBuilder
{
    uint64_t state = 0;

    void Add(const void* data, size_t len)
    {
        const uint64_t pair[2] = {state, Hash(data, len)};
        state = Hash(pair, sizeof(pair));
    }

    template<typename T>
        requires(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>)
    void Add(const T& value)
    {
        Add(&value, sizeof(T));
    }

    [[nodiscard]] uint64_t Finish() const { return state; }
};

#endif // WILL_ENGINE_XXH3_H
