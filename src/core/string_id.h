//
// Created by William on 2026-02-22.
//

#ifndef WILL_ENGINE_STRING_ID_H
#define WILL_ENGINE_STRING_ID_H

#include <cstdint>
#include <functional>
#include <string>

#include "hash/fnv_1_a.h"

#ifndef PACKAGED_BUILD
void DBG_InternString(uint64_t hash, const char* str);
const char* DBG_ResolveStringId(uint64_t hash);

// String interning uses function pointers instead of direct calls to cross the
// engine/game DLL boundary. Set these during game DLL load before using SID().
extern void (*gInternStringFn)(uint64_t, const char*);
extern const char* (*gResolveStringIdFn)(uint64_t);

#endif // !PACKAGED_BUILD

struct StringID
{
    uint64_t id = 0;

    constexpr StringID() = default;
    constexpr explicit StringID(uint64_t hash) : id(hash) {}

#ifndef PACKAGED_BUILD
    StringID(const char* str, size_t len);
#else
    constexpr StringID(const char* str, size_t len)
        : id(fnv1a64(str, len)) {}
#endif

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(StringID other) const { return id == other.id; }
    constexpr bool operator!=(StringID other) const { return id != other.id; }
    constexpr bool operator<(StringID other)  const { return id < other.id;  }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    const char* ToString() const;

    static const StringID Invalid;
};

inline const StringID StringID::Invalid{};


/**
 * Only works with string literals, use carefully
 * @param str
 */
#define SID(str) StringID(str, sizeof(str) - 1)

#ifndef PACKAGED_BUILD
inline StringID operator""_sid(const char* str, size_t len) {
    return StringID(str, len);
}
#else
constexpr StringID operator""_sid(const char* str, size_t len) {
    return StringID(str, len);
}
#endif

namespace std
{
template<>
struct hash<StringID>
{
    size_t operator()(StringID s) const noexcept
    {
        return static_cast<size_t>(s.id);
    }
};
}

inline StringID MakeConcatStringId(const char* a, size_t aLen,
                                   const char* b, size_t bLen)
{
    char buf[256];
    aLen = aLen < 255       ? aLen : 255;
    bLen = bLen < 255 - aLen ? bLen : 255 - aLen;
    memcpy(buf, a, aLen);
    memcpy(buf + aLen, b, bLen);
    buf[aLen + bLen] = '\0';
    return {buf, aLen + bLen};
}

inline StringID MakeConcatStringId(const std::string& a, const char* b, size_t bLen)
{
    return MakeConcatStringId(a.c_str(), a.size(), b, bLen);
}
#define SID_CONCAT(str, lit) MakeConcatStringId(str, lit, sizeof(lit) - 1)

#endif // WILL_ENGINE_STRING_ID_H