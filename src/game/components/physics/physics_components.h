//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_PHYSICS_COMPONENTS_H
#define WILL_ENGINE_PHYSICS_COMPONENTS_H

#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>

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

struct PendingPhysicsShapeCreationTag
{};

struct DrawPhysicsDebugTag
{};
}

#endif //WILL_ENGINE_PHYSICS_COMPONENTS_H
