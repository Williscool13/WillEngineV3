//
// Created by William on 2026-07-03.
//

#ifndef WILL_ENGINE_PHYSICS_COLLIDER_ASSET_H
#define WILL_ENGINE_PHYSICS_COLLIDER_ASSET_H

#include <cstdint>
#include <optional>

#include "core/containers/heap_array.h"
#include "core/containers/inline_string.h"
#include "core/containers/inline_path.h"
#include "core/memory/handle.h"
#include "core/types/math.h"
#include "engine/core/model_id.h"
#include "engine/core/physics_collider_id.h"
#include "engine/asset_manager_types.h"
#include "engine/resources/model/model_types.h"
#include "engine/resources/model/static_model.h"

namespace Engine
{
enum class PhysicsColliderKind : uint8_t
{
    Compound,
    ConvexHull,
    TriangleMesh,
};

enum class SplineColliderPrimitiveType : uint8_t
{
    Box,
    Capsule,
    Sphere,
    Cylinder,
    ConvexHull,
};

/**
 * One sub-shape of a Compound collider, in the collider's local space. Capsule/Cylinder axis is local Y (Jolt convention).
 * ConvexHull sub-shapes carry their vertices absolute (already positioned); position/rotation are identity and the verts live in the asset's `positions` pool at [hullOffset, hullOffset+hullCount).
 */
struct SplineColliderPrimitive
{
    SplineColliderPrimitiveType type{SplineColliderPrimitiveType::Box};
    Vec3 position{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 halfExtents{0.0f}; // Box
    float radius{0.0f}; // Capsule / Sphere / Cylinder
    float halfHeight{0.0f}; // Capsule / Cylinder
    uint32_t hullOffset{0}; // ConvexHull: first vertex index into the asset's positions pool
    uint32_t hullCount{0}; // ConvexHull: vertex count
};

/**
 * A CPU-only physics collider asset, built analytically from a source spec (spline/procedural/model/text). Keyed by hash(source) x kind.
 */
struct PhysicsColliderAsset
{
    enum class LoadState : uint8_t
    {
        NotLoaded,
        Loaded,
        FailedToLoad,
    };

    PhysicsColliderAsset() = default;

    ~PhysicsColliderAsset() = default;

    PhysicsColliderAsset(const PhysicsColliderAsset&) = delete;

    PhysicsColliderAsset& operator=(const PhysicsColliderAsset&) = delete;

    PhysicsColliderAsset(PhysicsColliderAsset&&) noexcept = default;

    PhysicsColliderAsset& operator=(PhysicsColliderAsset&&) noexcept = default;

    // Populated by AssetManager
    Core::InlineString<128> name{};
    PhysicsColliderHandle selfHandle{};
    PhysicsColliderID colliderId{};
    PhysicsColliderKind kind{PhysicsColliderKind::Compound};
    LoadState loadState{LoadState::NotLoaded};
    uint32_t refCount{0};
    uint64_t acquireFrame{UINT64_MAX};
    uint64_t retireFrame{0};

    // Source spec (exactly one populated). Mirrors StaticModel; the disk source (model/font) freeze-gates hot-reload.
    Engine::ModelID sourceModelId{};
    Core::Path source{};
    std::optional<ProceduralParams> proceduralParams{};
    std::optional<SplineParams> splineParams{};
    std::optional<Text3DParams> text3DParams{};
    FontHandle text3DFontHandle{};
    bool bPreciseText3D{false};

    // Payload (written by the worker into memoryManager->Assets()).
    Core::HeapArray<SplineColliderPrimitive> primitives{}; // Compound
    Core::HeapArray<Vec3> positions{}; // ConvexHull / TriangleMesh
    Core::HeapArray<uint32_t> indices{}; // TriangleMesh

    ModelBounds bounds{};
};
} // Engine

#endif //WILL_ENGINE_PHYSICS_COLLIDER_ASSET_H
