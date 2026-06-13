//
// Created by William on 2026-06-13.
//

#ifndef WILL_ENGINE_PHYSICS_SHAPE_HELPERS_H
#define WILL_ENGINE_PHYSICS_SHAPE_HELPERS_H

#include <glm/glm.hpp>

#include "physics_body_desc.h"
#include "engine/resources/model/model_types.h"

namespace Game::Component
{
struct SplineMeshComponent;

void FillSplineParams(Engine::SplineParams& out, const SplineMeshComponent& splm);

/**
 * Builds the default collider for a procedural mesh: a matching primitive (box/sphere/capsule) where one fits exactly,
 * otherwise a ConvexHull/TriangleMesh reusing the procedural mesh. Primitive dimensions are pre-scaled by `scale`.
 */
PhysicsShapeDesc MakeProceduralShape(const Engine::ProceduralParams& params, const glm::vec3& scale);
} // Game::Component

#endif //WILL_ENGINE_PHYSICS_SHAPE_HELPERS_H
