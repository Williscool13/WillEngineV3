//
// Created by William on 2026-02-22.
//

#ifndef WILL_ENGINE_STRING_ID_H
#define WILL_ENGINE_STRING_ID_H
#include <cstdint>
#include <functional>

constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

constexpr uint64_t fnv1a64(const char* str, size_t len)
{
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(str[i]);
        hash *= FNV_PRIME;
    }
    return hash;
}

struct StringId
{
    uint64_t id = 0;

    constexpr StringId() = default;

    constexpr explicit StringId(uint64_t hash) : id(hash) {}

    constexpr StringId(const char* str, size_t len)
        : id(fnv1a64(str, len))
    {}

    constexpr bool operator==(StringId other) const { return id == other.id; }
    constexpr bool operator!=(StringId other) const { return id != other.id; }
    constexpr bool operator<(StringId other) const { return id < other.id; }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const StringId Invalid;
};

inline const StringId StringId::Invalid{};

#define SID(str) StringId(str, sizeof(str) - 1)

namespace std
{
template<>
struct hash<StringId>
{
    size_t operator()(StringId s) const noexcept
    {
        return static_cast<size_t>(s.id);
    }
};
}

inline StringId MakeConcatStringId(const char* a, size_t aLen,
                                   const char* b, size_t bLen)
{
    char buf[256];
    aLen = aLen < 255 ? aLen : 255;
    bLen = bLen < 255 - aLen ? bLen : 255 - aLen;
    memcpy(buf, a, aLen);
    memcpy(buf + aLen, b, bLen);
    buf[aLen + bLen] = '\0';
    return {buf, aLen + bLen};
}

#endif //WILL_ENGINE_STRING_ID_H
