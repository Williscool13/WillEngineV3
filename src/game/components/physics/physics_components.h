//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_PHYSICS_COMPONENTS_H
#define WILL_ENGINE_PHYSICS_COMPONENTS_H

#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>
#include <entt/entt.hpp>

#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
/** Physics state. Interpolate w/ physics alpha for smooth physics*/
struct DynamicPhysicsBodyComponent
{
    glm::vec3 previousPosition{};
    glm::quat previousRotation{};
    glm::vec3 currentPosition{};
    glm::quat currentRotation{};
};

struct TeleportPhysicsTransformTag
{};

struct DirtyKinematicPhysicsTransformTag
{};

struct PendingPhysicsMeshTag
{};

struct PhysicsMeshLoadingTag
{};

struct PendingPhysicsShapeCreationTag
{};

struct PendingPhysicsBodyCreationTag
{};

struct SetVelocityTag
{
    glm::vec3 linearVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
};

struct DrawPhysicsDebugTag
{
    static constexpr const char* COMPONENT_NAME = "DrawPhysicsDebugTag";
};
}

#endif //WILL_ENGINE_PHYSICS_COMPONENTS_H
