//
// Created by William on 2026-02-22.
//

#ifndef WILL_ENGINE_STRING_ID_H
#define WILL_ENGINE_STRING_ID_H

#include <cstdint>
#include <functional>
#include <string>

#include "containers/inline_string.h"
#include "hash/xxh3.h"

#ifdef WDEBUG
void DBG_InternString(uint64_t hash, const char* str);
const char* DBG_ResolveStringId(uint64_t hash);

// String interning uses function pointers instead of direct calls to cross the
// engine/game DLL boundary. Set these during game DLL load before using _sid.
extern void (*gInternStringFn)(uint64_t, const char*);
extern const char* (*gResolveStringIdFn)(uint64_t);

#endif // WDEBUG

struct StringID
{
    uint64_t id = 0;

    constexpr StringID() = default;
    constexpr explicit StringID(uint64_t hash) : id(hash) {}

#ifdef WDEBUG
    StringID(const char* str, size_t len);
#else
    constexpr StringID(const char* str, size_t len)
        : id(Hash(str, len)) {}
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

constexpr size_t StringIdLength(const char* str)
{
    size_t n = 0;
    while (str[n] != '\0') { ++n; }
    return n;
}


#ifdef WDEBUG
inline StringID operator""_sid(const char* str, size_t len) {
    return {str, len};
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

#endif // WILL_ENGINE_STRING_ID_H