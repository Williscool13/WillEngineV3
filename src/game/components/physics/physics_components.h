//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_PHYSICS_COMPONENTS_H
#define WILL_ENGINE_PHYSICS_COMPONENTS_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>


namespace Game::Component
{
struct PhysicsBodyComponent
{
    JPH::BodyID bodyID;

    static void on_construct(entt::registry& registry, entt::entity entity);
    static void on_destroy(entt::registry& registry, entt::entity entity);
};

struct DynamicPhysicsBodyComponent
{
    glm::vec3 previousPosition{};
    glm::quat previousRotation{};
};

class DirtyPhysicsTransformComponent
{};

struct DrawPhysicsDebugComponent
{};
}

#endif //WILL_ENGINE_PHYSICS_COMPONENTS_H
