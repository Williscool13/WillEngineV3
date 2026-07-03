//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PHYSICS_BODY_DESC_H
#define WILL_ENGINE_PHYSICS_BODY_DESC_H

#include <Jolt/Jolt.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <json/nlohmann/json_fwd.hpp>
#include <glm/detail/type_quat.hpp>

#include "core/containers/inline_vector.h"
#include "Jolt/Physics/Body/MotionQuality.h"
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "engine/core/model_id.h"
#include "engine/core/font_id.h"
#include "engine/asset_manager_types.h"
#include "engine/resources/model/model_types.h"
#include "engine/engine_api.h"
#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
enum class PhysicsShapeType : uint8_t
{
    Box,
    Sphere,
    Capsule,
    ConvexHull,
    TriangleMesh,
    Compound,
};

enum class PhysicsMotionType : uint8_t
{
    Static,
    Kinematic,
    Dynamic,
};

/** Identity needed to re-derive a 3D-text collision mesh on load (mirrors the component's fields). Valid when fontId is set and text is non-empty. */
struct Text3DShapeSource
{
    Engine::FontID fontId{};
    Core::InlineString<256> text{};
    float depth{0.2f};
    float flatness{0.005f};
    float tracking{0.0f};
    float scale{1.0f};
    bool bSmoothNormals{true};

    bool IsValid() const { return fontId.IsValid() && text.Size() > 0; }
};

struct PhysicsShapeDesc
{
    PhysicsShapeType type{PhysicsShapeType::Box};
    glm::vec3 offset{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 bakedScale{1.0f};

    // Only used for ConvexHull / TriangleMesh (mutually exclusive; modelId has priority, then procedural, spline, text3D).
    Engine::ModelID meshSourceModelId{};
    Engine::ProceduralParams proceduralParams{};
    Engine::SplineParams splineParams{};
    Text3DShapeSource text3DSource{};

    // Transient
    Engine::StaticModelHandle meshSourceHandle{};
    Engine::PhysicsColliderHandle colliderHandle{};

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
    float friction{0.5f};
    float restitution{0.0f};
    JPH::EMotionQuality motionQuality{JPH::EMotionQuality::Discrete};
    JPH::ObjectLayer layerOverride{0xFFFF};
    bool bActive{true};
    bool bEnhancedInternalEdgeRemoval{false};
    bool bIsSensor{false};

    Core::InlineVector<PhysicsShapeDesc, 8> shapes;

    // potentially also store its type (e.g. compound)
    JPH::ShapeRefC shapeRef;

    static void Serialize(const PhysicsBodyDesc& comp, nlohmann::json& json);
    static void Deserialize(PhysicsBodyDesc& comp, const nlohmann::json& json);
    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnUpdate(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_PHYSICS_BODY_DESC_H
