//
// Created by William on 2026-07-03.
//

#ifndef WILL_ENGINE_COLLIDER_GENERATION_H
#define WILL_ENGINE_COLLIDER_GENERATION_H

#include "core/containers/span.h"
#include "core/containers/vector.h"
#include "engine/resources/model/model_types.h"
#include "engine/resources/physics/physics_collider_asset.h"
#include "engine/spline/spline_frames.h"

namespace Engine
{
/**
 * Builds a Compound collider (oriented boxes, or capsules for the round Tube profile) that follows a spline: per-segment sweep primitives, plus railing posts and cross-planks.
 * Mirrors the render sweep's lane/post/plank layout off the same sampled frames, so the collider tracks the visual mesh. Appends to `out`.
 */
void BuildSplineColliderPrimitives(const SplineParams& params, Core::Span<const SplineFrame> frames, Core::Vector<SplineColliderPrimitive>& out);

/** True if the procedural type has an analytic collider builder (else the caller falls back to the render-mesh physics path). */
bool CanBuildProceduralCollider(const ProceduralParams& params);

/**
 * Builds an analytic collider for a procedural shape, matching the render generator's pivot/orientation. Sets `outKind` and fills exactly one payload:
 * ConvexHull writes `outPositions`; Compound writes `outPrimitives`. Returns false for types without an analytic builder.
 */
bool BuildProceduralCollider(const ProceduralParams& params, PhysicsColliderKind& outKind, Core::Vector<SplineColliderPrimitive>& outPrimitives, Core::Vector<Vec3>& outPositions);
} // Engine

#endif //WILL_ENGINE_COLLIDER_GENERATION_H
