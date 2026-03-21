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
#include "core/string_id.h"
#include "engine/core/model_id.h"
#include "engine/asset_manager_types.h"
#include "engine/resources/model/model_types.h"
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"


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
    ConvexHull,
    TriangleMesh,
};

enum class PhysicsMotionType : uint8_t
{
    Static,
    Kinematic,
    Dynamic,
};

struct PhysicsShapeDesc
{
    PhysicsShapeType type{PhysicsShapeType::Box};
    glm::vec3 offset{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    // Only used for ConvexHull / TriangleMesh (Mutually exclusive, if modelID has priority).
    Engine::ModelID meshSourceModelId{};
    Engine::ProceduralParams proceduralParams{};
    Engine::SplineParams splineParams{};

    // Transient
    Engine::StaticModelHandle meshSourceHandle{};

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
    float friction{0.0f};
    float restitution{0.0f};
    JPH::EMotionQuality motionQuality{JPH::EMotionQuality::Discrete};
    JPH::ObjectLayer layerOverride{0xFFFF};
    bool active{true};

    std::vector<PhysicsShapeDesc> shapes;

    // potentially also store its type (e.g. compound)
    JPH::ShapeRefC shapeRef;
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

struct PendingPhysicsMeshTag
{};

struct PendingPhysicsShapeCreationTag
{};

struct PendingPhysicsBodyCreationTag
{};

struct DrawPhysicsDebugTag
{};
}

#endif //WILL_ENGINE_PHYSICS_COMPONENTS_H
