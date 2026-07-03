//
// Created by William on 2026-07-03.
//

#ifndef WILL_ENGINE_PHYSICS_COLLIDER_ID_H
#define WILL_ENGINE_PHYSICS_COLLIDER_ID_H

#include <cstdint>

#include "core/containers/hash.h"

namespace Engine
{
struct PhysicsColliderID
{
    uint64_t id = 0;

    constexpr PhysicsColliderID() = default;
    constexpr explicit PhysicsColliderID(uint64_t id) : id(id) {}

    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(PhysicsColliderID other) const { return id == other.id; }
    constexpr bool operator!=(PhysicsColliderID other) const { return id != other.id; }
    constexpr bool operator<(PhysicsColliderID other) const { return id < other.id; }

    [[nodiscard]] constexpr bool IsValid() const { return id != 0; }

    static const PhysicsColliderID INVALID;
};

inline const PhysicsColliderID PhysicsColliderID::INVALID{};
} // Engine

namespace Core
{
template<> struct Hash<Engine::PhysicsColliderID> { uint64_t operator()(Engine::PhysicsColliderID m) const { return m.id; } };
}

#endif //WILL_ENGINE_PHYSICS_COLLIDER_ID_H
