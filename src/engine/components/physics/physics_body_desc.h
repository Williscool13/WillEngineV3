//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PHYSICS_BODY_DESC_H
#define WILL_ENGINE_PHYSICS_BODY_DESC_H

#include <Jolt/Jolt.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>

#include "core/containers/inline_vector.h"
#include "Jolt/Physics/Body/MotionQuality.h"
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "Jolt/Physics/Collision/Shape/Shape.h"
#include "engine/core/model_id.h"
#include "engine/core/font_id.h"
#include "engine/asset_manager_types.h"
#include "engine/resources/model/model_types.h"
#include "engine/component_registry.h"
#include "engine/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Engine::Component
{
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
    float wrapWidth{0.0f};
    float bendRadius{0.0f};
    bool bSmoothNormals{true};
    Engine::Text3DAlign align{Engine::Text3DAlign::Left};
    Engine::Text3DAnchor anchor{Engine::Text3DAnchor::Baseline};
    bool bPrecise{false};

    WILL_REFLECT(Text3DShapeSource, WILL_FIELD(fontId), WILL_FIELD(text), WILL_FIELD(depth), WILL_FIELD(flatness), WILL_FIELD(tracking), WILL_FIELD(scale),
                 WILL_FIELD(wrapWidth), WILL_FIELD(bendRadius), WILL_FIELD(bSmoothNormals, .key = "smoothNormals"), WILL_FIELD(align), WILL_FIELD(anchor),
                 WILL_FIELD(bPrecise, .key = "precise"))

    bool IsValid() const { return fontId.IsValid() && text.Size() > 0; }
};

struct BoxShape
{
    glm::vec3 halfExtents{0.5f};

    WILL_REFLECT(BoxShape, WILL_FIELD(halfExtents))
};

struct SphereShape
{
    float radius{0.5f};

    WILL_REFLECT(SphereShape, WILL_FIELD(radius))
};

struct CapsuleShape
{
    float radius{0.5f};
    float halfHeight{0.5f};

    WILL_REFLECT(CapsuleShape, WILL_FIELD(radius), WILL_FIELD(halfHeight))
};

/** Mutually exclusive sources: modelId has priority, then procedural, spline, text3D. */
struct ColliderShape
{
    Engine::ModelID meshSourceModelId{};
    bool bMeshPrecise{false};
    Engine::ProceduralParams proceduralParams{};
    Engine::SplineParams splineParams{};
    Text3DShapeSource text3DSource{};

    WILL_REFLECT(ColliderShape, WILL_FIELD(meshSourceModelId), WILL_FIELD(bMeshPrecise, .key = "meshPrecise"),
                 WILL_FIELD(proceduralParams, .key = "proceduralType", .flags = Engine::FIELD_FLATTEN), WILL_FIELD(splineParams), WILL_FIELD(text3DSource))

    [[nodiscard]] bool HasSource() const
    {
        return meshSourceModelId.IsValid() || !std::holds_alternative<std::monostate>(proceduralParams) || !splineParams.spline.points.IsEmpty() || text3DSource.IsValid();
    }
};

using PhysicsShapeGeometry = std::variant<BoxShape, SphereShape, CapsuleShape, ColliderShape>;

struct PhysicsShapeDesc
{
    PhysicsShapeGeometry geometry{};
    glm::vec3 offset{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 bakedScale{1.0f};

    WILL_REFLECT(PhysicsShapeDesc, WILL_FIELD(geometry, .key = "type", .flags = Engine::FIELD_FLATTEN), WILL_FIELD(offset), WILL_FIELD(rotation), WILL_FIELD(bakedScale))
};

/** Runtime state derived from PhysicsBodyDesc; colliders is index-parallel to shapes and rebuilt with them. */
struct PhysicsShapeRuntime
{
    Core::InlineVector<Engine::PhysicsColliderHandle, 8> colliders;
    // potentially also store its type (e.g. compound)
    JPH::ShapeRefC shapeRef;
};

struct PhysicsBodyDesc
{
    static constexpr const char* COMPONENT_NAME = "PhysicsBodyDesc";

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

    WILL_REFLECT(PhysicsBodyDesc,
        WILL_FIELD(motionType),
        WILL_FIELD(mass),
        WILL_FIELD(friction),
        WILL_FIELD(restitution),
        WILL_FIELD(motionQuality),
        WILL_FIELD(layerOverride),
        WILL_FIELD(bEnhancedInternalEdgeRemoval, .key = "enhancedInternalEdgeRemoval"),
        WILL_FIELD(bIsSensor, .key = "isSensor"),
        WILL_FIELD(shapes))

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
    static void DeferredConstruct(entt::registry& registry, entt::entity entity);
    /** Releases the runtime colliders and queues a DeferredConstruct. */
    static void RequestRebuild(entt::registry& registry, entt::entity entity);
    static void OnEditCommit(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};
}

#endif //WILL_ENGINE_PHYSICS_BODY_DESC_H
