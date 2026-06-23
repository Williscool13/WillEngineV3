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
struct DynamicPhysicsBodyComponent
{
    glm::vec3 previousPosition{};
    glm::quat previousRotation{};
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
{};
}

#endif //WILL_ENGINE_PHYSICS_COMPONENTS_H
