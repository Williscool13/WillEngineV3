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

#include "Jolt/Physics/Body/MotionQuality.h"


namespace Game::Component
{
struct PhysicsBodyComponent
{
    JPH::BodyID bodyID;

    static void on_construct(entt::registry& registry, entt::entity entity);

    static void on_destroy(entt::registry& registry, entt::entity entity);
};

enum class PhysicsShapeType : uint8_t
{
    Box,
    Sphere,
    Capsule,
};

enum class PhysicsMotionType : uint8_t
{
    Static,
    Kinematic,
    Dynamic,
};

struct PhysicsShapeDesc
{
    PhysicsShapeType type;
    glm::vec3 offset{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    union
    {
        struct
        {
            glm::vec3 halfExtents;
        } box;

        struct
        {
            float radius;
        } sphere;

        struct
        {
            float radius;
            float halfHeight;
        } capsule;
    };
};

struct PhysicsBodyDesc
{
    PhysicsMotionType motionType{PhysicsMotionType::Static};
    float mass{1.0f};
    JPH::EMotionQuality motionQuality{JPH::EMotionQuality::Discrete};
    bool active{true};

    std::vector<PhysicsShapeDesc> shapes;
};

struct DynamicPhysicsBodyComponent
{
    glm::vec3 previousPosition{};
    glm::quat previousRotation{};
};

struct TeleportPhysicsTransformTag
{};

struct DirtyKinematicPhysicsTransformTag
{};

struct DrawPhysicsDebugTag
{};
}

#endif //WILL_ENGINE_PHYSICS_COMPONENTS_H
