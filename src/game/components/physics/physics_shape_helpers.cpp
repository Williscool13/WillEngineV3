//
// Created by William on 2026-06-13.
//

#include "physics_shape_helpers.h"

#include <variant>
#include <type_traits>

#include "game/components/render/spline_mesh_component.h"

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
    out.bDualPath = splm.bDualPath;
    out.dualPathSpacing = splm.dualPathSpacing;
    out.bCrossPlanks = splm.bCrossPlanks;
    out.crossPlankInterval = splm.crossPlankInterval;
    out.crossPlankHeight = splm.crossPlankHeight;
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
            shape.type = PhysicsShapeType::ConvexHull;
            shape.proceduralParams = params;
            shape.bakedScale = scale;
        }
        else if constexpr (std::is_same_v<T, std::monostate>) {
            shape.type = PhysicsShapeType::Box;
            shape.box.halfExtents = glm::vec3(0.5f) * scale;
        }
        else {
            shape.type = PhysicsShapeType::TriangleMesh;
            shape.proceduralParams = params;
            shape.bakedScale = scale;
        }
    }, params);
    return shape;
}
} // Game::Component
