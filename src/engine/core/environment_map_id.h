//
// Created by William on 2026-04-15.
//

#ifndef WILL_ENGINE_ENVIRONMENT_MAP_ID_H
#define WILL_ENGINE_ENVIRONMENT_MAP_ID_H

#include <cstdint>
#include <functional>

#include "core/containers/hash.h"

namespace Engine
{
struct EnvironmentMapID
{
    uint64_t id = 0;

    constexpr EnvironmentMapID() = default;
    constexpr explicit EnvironmentMapID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(EnvironmentMapID other) const { return id == other.id; }
    constexpr bool operator!=(EnvironmentMapID other) const { return id != other.id; }
    constexpr bool operator<(EnvironmentMapID other)  const { return id < other.id;  }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const EnvironmentMapID INVALID;
};

inline const EnvironmentMapID EnvironmentMapID::INVALID{};
} // Engine

namespace std
{
template<>
struct hash<Engine::EnvironmentMapID>
{
    size_t operator()(Engine::EnvironmentMapID e) const noexcept
    {
        return e.id;
    }
};
}

namespace Core
{
template<> struct Hash<Engine::EnvironmentMapID> { uint64_t operator()(Engine::EnvironmentMapID e) const { return e.id; } };
}

#endif //WILL_ENGINE_ENVIRONMENT_MAP_ID_H
