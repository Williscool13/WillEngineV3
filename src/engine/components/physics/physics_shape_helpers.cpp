//
// Created by William on 2026-06-13.
//

#include "physics_shape_helpers.h"

#include <variant>
#include <type_traits>

#include "engine/resources/model/static_model.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/components/render/text3d_component.h"

namespace Engine::Component
{
void FillSplineParams(Engine::SplineParams& out, const SplineMeshComponent& splm)
{
    out.spline = splm.spline;
    out.radius = splm.radius;
    out.rollAngle = splm.rollAngle;
    out.sides = splm.sides;
    out.segmentsPerSpan = splm.segmentsPerSpan;
    out.bCaps = splm.bCaps;
    out.bCrossPlanks = splm.bCrossPlanks;
    out.profile = splm.profile;
    out.crossPlankInterval = splm.crossPlankInterval;
    out.crossPlankHeight = splm.crossPlankHeight;
    out.crossPlankThickness = splm.crossPlankThickness;
    out.crossPlankLength = splm.crossPlankLength;
    out.railing = splm.railing;
}

PhysicsShapeDesc MakeProceduralShape(const Engine::ProceduralParams& params, const Engine::ProceduralRepeat& repeat, const glm::vec3& scale)
{
    const float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
    PhysicsShapeDesc shape{};
    if (repeat.IsActive()) {
        ColliderShape collider{};
        collider.proceduralParams = params;
        collider.proceduralRepeat = repeat;
        shape.geometry = collider;
        shape.bakedScale = scale;
        return shape;
    }
    std::visit([&](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, Engine::BoxParams>) {
            const glm::vec3 he = glm::vec3(p.sizeX, p.sizeY, p.sizeZ) * 0.5f * scale;
            shape.geometry = BoxShape{he};
            shape.offset = he; // procedural box uses a corner pivot
        }
        else if constexpr (std::is_same_v<T, Engine::PlaneParams>) {
            shape.geometry = BoxShape{glm::vec3(p.sizeX * 0.5f, 0.05f, p.sizeZ * 0.5f) * scale};
        }
        else if constexpr (std::is_same_v<T, Engine::SphereParams> || std::is_same_v<T, Engine::SubdividedSphereParams>) {
            shape.geometry = SphereShape{p.radius * maxScale};
        }
        else if constexpr (std::is_same_v<T, Engine::CapsuleParams>) {
            shape.geometry = CapsuleShape{p.radius * glm::max(scale.x, scale.z), glm::max(0.001f, (p.height * 0.5f - p.radius) * scale.y)};
        }
        else if constexpr (std::is_same_v<T, std::monostate>) {
            shape.geometry = BoxShape{glm::vec3(0.5f) * scale};
        }
        else {
            ColliderShape collider{};
            collider.proceduralParams = params;
            shape.geometry = collider;
            shape.bakedScale = scale;
        }
    }, params);
    return shape;
}

void FitMeshShapeToEntity(entt::registry& registry, entt::entity entity, PhysicsShapeDesc& shape, const glm::vec3& scale)
{
    ColliderShape collider{};
    shape.bakedScale = scale;

    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};
    if (auto* sm = registry.try_get<StaticMeshComponent>(entity)) {
        collider.meshSourceModelId = sm->modelId;
        renderOffset = sm->renderOffset;
        renderRotation = sm->renderRotation;
    }
    else if (auto* pm = registry.try_get<ProceduralMeshComponent>(entity)) {
        collider.proceduralParams = pm->params;
        collider.proceduralRepeat = pm->repeat;
        renderOffset = pm->renderOffset;
        renderRotation = pm->renderRotation;
    }
    else if (auto* splm = registry.try_get<SplineMeshComponent>(entity)) {
        FillSplineParams(collider.splineParams, *splm);
    }
    else if (auto* t3 = registry.try_get<Text3DComponent>(entity)) {
        collider.text3DSource.fontId = t3->fontId;
        collider.text3DSource.text = t3->text;
        collider.text3DSource.depth = t3->depth;
        collider.text3DSource.flatness = t3->flatness;
        collider.text3DSource.tracking = t3->tracking;
        collider.text3DSource.scale = t3->scale;
        collider.text3DSource.wrapWidth = t3->wrapWidth;
        collider.text3DSource.bendRadius = t3->bendRadius;
        collider.text3DSource.bSmoothNormals = t3->bSmoothNormals;
        collider.text3DSource.align = t3->align;
        collider.text3DSource.anchor = t3->anchor;
        renderOffset = t3->renderOffset;
        renderRotation = t3->renderRotation;
    }

    shape.geometry = collider;
    shape.offset = scale * renderOffset;
    shape.rotation = renderRotation;
}

void FitPrimitiveShapeToEntity(entt::registry& registry, entt::entity entity, PhysicsShapeDesc& shape, const glm::vec3& scale, const Engine::ModelBounds& bounds)
{
    shape.bakedScale = glm::vec3(1.0f);

    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};
    if (auto* sm = registry.try_get<StaticMeshComponent>(entity)) {
        renderOffset = sm->renderOffset;
        renderRotation = sm->renderRotation;
    }
    else if (auto* pm = registry.try_get<ProceduralMeshComponent>(entity)) {
        renderOffset = pm->renderOffset;
        renderRotation = pm->renderRotation;
    }
    else if (auto* t3 = registry.try_get<Text3DComponent>(entity)) {
        renderOffset = t3->renderOffset;
        renderRotation = t3->renderRotation;
    }

    if (auto* box = std::get_if<BoxShape>(&shape.geometry)) {
        box->halfExtents = bounds.aabb.HalfExtents() * scale;
        shape.offset = bounds.aabb.Center() * scale;
    }
    else if (auto* sphere = std::get_if<SphereShape>(&shape.geometry)) {
        const float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
        sphere->radius = bounds.sphere.radius * maxScale;
        shape.offset = bounds.sphere.center * scale;
    }
    else if (auto* capsule = std::get_if<CapsuleShape>(&shape.geometry)) {
        const glm::vec3 he = bounds.aabb.HalfExtents() * scale;
        capsule->radius = glm::max(he.x, he.z);
        capsule->halfHeight = glm::max(0.001f, he.y - capsule->radius);
        shape.offset = bounds.aabb.Center() * scale;
    }

    shape.offset = scale * renderOffset + renderRotation * shape.offset;
    shape.rotation = renderRotation;
}
} // Engine::Component
