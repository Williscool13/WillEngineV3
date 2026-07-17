//
// Created by William on 2026-06-13.
//

#include "physics_shape_helpers.h"

#include <variant>
#include <type_traits>

#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/text3d_component.h"

namespace Game::Component
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

PhysicsShapeDesc MakeProceduralShape(const Engine::ProceduralParams& params, const glm::vec3& scale)
{
    const float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
    PhysicsShapeDesc shape{};
    std::visit([&](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, Engine::BoxParams>) {
            const glm::vec3 he = glm::vec3(p.sizeX, p.sizeY, p.sizeZ) * 0.5f * scale;
            shape.type = PhysicsShapeType::Box;
            shape.box.halfExtents = he;
            shape.offset = he; // procedural box uses a corner pivot
        }
        else if constexpr (std::is_same_v<T, Engine::PlaneParams>) {
            shape.type = PhysicsShapeType::Box;
            shape.box.halfExtents = glm::vec3(p.sizeX * 0.5f, 0.05f, p.sizeZ * 0.5f) * scale;
        }
        else if constexpr (std::is_same_v<T, Engine::SphereParams> || std::is_same_v<T, Engine::SubdividedSphereParams>) {
            shape.type = PhysicsShapeType::Sphere;
            shape.sphere.radius = p.radius * maxScale;
        }
        else if constexpr (std::is_same_v<T, Engine::CapsuleParams>) {
            shape.type = PhysicsShapeType::Capsule;
            shape.capsule.radius = p.radius * glm::max(scale.x, scale.z);
            shape.capsule.halfHeight = glm::max(0.001f, (p.height * 0.5f - p.radius) * scale.y);
        }
        else if constexpr (std::is_same_v<T, Engine::CylinderParams> || std::is_same_v<T, Engine::ConeParams> || std::is_same_v<T, Engine::WedgeParams>
                           || std::is_same_v<T, Engine::HemisphereParams> || std::is_same_v<T, Engine::TetrahedronParams>
                           || std::is_same_v<T, Engine::OctahedronParams> || std::is_same_v<T, Engine::IcosahedronParams>
                           || std::is_same_v<T, Engine::DodecahedronParams>) {
            shape.type = PhysicsShapeType::Collider;
            shape.proceduralParams = params;
            shape.bakedScale = scale;
        }
        else if constexpr (std::is_same_v<T, std::monostate>) {
            shape.type = PhysicsShapeType::Box;
            shape.box.halfExtents = glm::vec3(0.5f) * scale;
        }
        else {
            shape.type = PhysicsShapeType::Collider;
            shape.proceduralParams = params;
            shape.bakedScale = scale;
        }
    }, params);
    return shape;
}

void FitMeshShapeToEntity(entt::registry& registry, entt::entity entity, PhysicsShapeDesc& shape, const glm::vec3& scale)
{
    shape.meshSourceModelId = Engine::ModelID::INVALID;
    shape.proceduralParams = std::monostate{};
    shape.splineParams.spline.points.Clear();
    shape.text3DSource = {};
    shape.bakedScale = scale;

    glm::vec3 renderOffset{0.0f};
    glm::quat renderRotation{1.0f, 0.0f, 0.0f, 0.0f};
    if (auto* sm = registry.try_get<StaticMeshComponent>(entity)) {
        shape.meshSourceModelId = sm->modelId;
        renderOffset = sm->renderOffset;
        renderRotation = sm->renderRotation;
    }
    else if (auto* pm = registry.try_get<ProceduralMeshComponent>(entity)) {
        shape.proceduralParams = pm->params;
        renderOffset = pm->renderOffset;
        renderRotation = pm->renderRotation;
    }
    else if (auto* splm = registry.try_get<SplineMeshComponent>(entity)) {
        FillSplineParams(shape.splineParams, *splm);
    }
    else if (auto* t3 = registry.try_get<Text3DComponent>(entity)) {
        shape.text3DSource.fontId = t3->fontId;
        shape.text3DSource.text = t3->text;
        shape.text3DSource.depth = t3->depth;
        shape.text3DSource.flatness = t3->flatness;
        shape.text3DSource.tracking = t3->tracking;
        shape.text3DSource.scale = t3->scale;
        shape.text3DSource.bSmoothNormals = t3->bSmoothNormals;
        shape.text3DSource.align = t3->align;
        shape.text3DSource.anchor = t3->anchor;
        renderOffset = t3->renderOffset;
        renderRotation = t3->renderRotation;
    }

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

    switch (shape.type) {
        case PhysicsShapeType::Box:
            shape.box.halfExtents = bounds.aabb.HalfExtents() * scale;
            shape.offset = bounds.aabb.Center() * scale;
            break;
        case PhysicsShapeType::Sphere:
        {
            const float maxScale = glm::max(scale.x, glm::max(scale.y, scale.z));
            shape.sphere.radius = bounds.sphere.radius * maxScale;
            shape.offset = bounds.sphere.center * scale;
            break;
        }
        case PhysicsShapeType::Capsule:
        {
            const glm::vec3 he = bounds.aabb.HalfExtents() * scale;
            const float radius = glm::max(he.x, he.z);
            shape.capsule.radius = radius;
            shape.capsule.halfHeight = glm::max(0.001f, he.y - radius);
            shape.offset = bounds.aabb.Center() * scale;
            break;
        }
        default:
            break;
    }

    shape.offset = scale * renderOffset + renderRotation * shape.offset;
    shape.rotation = renderRotation;
}
} // Game::Component
