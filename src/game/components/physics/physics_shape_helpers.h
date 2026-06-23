//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_PHYSICS_SHAPE_HELPERS_H
#define WILL_ENGINE_PHYSICS_SHAPE_HELPERS_H

#include <glm/glm.hpp>

#include "physics_body_desc.h"
#include "engine/resources/model/model_types.h"

namespace Engine { struct ModelBounds; }

namespace Game::Component
{
struct SplineMeshComponent;

void FillSplineParams(Engine::SplineParams& out, const SplineMeshComponent& splm);

/**
 * Builds the default collider for a procedural mesh: a matching primitive (box/sphere/capsule) where one fits exactly,
 * otherwise a ConvexHull/TriangleMesh reusing the procedural mesh. Primitive dimensions are pre-scaled by `scale`.
 */
PhysicsShapeDesc MakeProceduralShape(const Engine::ProceduralParams& params, const glm::vec3& scale);

/**
 * Populates a mesh shape (ConvexHull/TriangleMesh) from the entity's attached mesh, resolving the source and baked
 * transform in StaticMesh > Procedural > Spline > Text3D priority. Clears the source and bakes an identity transform
 * when no mesh component is attached.
 */
void FitMeshShapeToEntity(entt::registry& registry, entt::entity entity, PhysicsShapeDesc& shape, const glm::vec3& scale);

/**
 * Fits a primitive shape (Box/Sphere/Capsule, per shape.type) to the given model bounds, pre-scaled by `scale`, and
 * bakes the attached mesh's render offset/rotation. shape.type must be set before calling.
 */
void FitPrimitiveShapeToEntity(entt::registry& registry, entt::entity entity, PhysicsShapeDesc& shape, const glm::vec3& scale, const Engine::ModelBounds& bounds);
} // Game::Component

#endif //WILL_ENGINE_PHYSICS_SHAPE_HELPERS_H
