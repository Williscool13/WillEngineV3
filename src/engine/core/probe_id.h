//
// Created by William on 2026-07-22.
//

#ifndef WILL_ENGINE_PROBE_ID_H
#define WILL_ENGINE_PROBE_ID_H

#include <cstdint>
#include <functional>

#include "core/containers/hash.h"

namespace Engine
{
struct ProbeID
{
    uint64_t id = 0;

    constexpr ProbeID() = default;
    constexpr explicit ProbeID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(ProbeID other) const { return id == other.id; }
    constexpr bool operator!=(ProbeID other) const { return id != other.id; }
    constexpr bool operator<(ProbeID other) const { return id < other.id; }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const ProbeID INVALID;
};

inline const ProbeID ProbeID::INVALID{};
} // Engine

namespace std
{
template<>
struct hash<Engine::ProbeID>
{
    size_t operator()(Engine::ProbeID e) const noexcept
    {
        return e.id;
    }
};
}

namespace Core
{
template<> struct Hash<Engine::ProbeID> { uint64_t operator()(Engine::ProbeID e) const { return e.id; } };
}

#endif //WILL_ENGINE_PROBE_ID_H
