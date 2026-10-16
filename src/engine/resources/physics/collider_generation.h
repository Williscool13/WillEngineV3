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
struct Font;

/**
 * Builds a Compound collider (oriented boxes, or capsules for the round Tube profile) that follows a spline: per-segment sweep primitives, plus railing posts and cross-planks.
 * Mirrors the render sweep's lane/post/plank layout off the same sampled frames, so the collider tracks the visual mesh. Appends to `out`.
 */
void BuildSplineColliderPrimitives(const SplineParams& params, Core::Span<const SplineFrame> frames, Core::Vector<SplineColliderPrimitive>& out);

/** True for a single-lane Tube profile, the only spline that has a hollow collider. */
bool CanBuildHollowSplineCollider(const SplineParams& params);

/**
 * Builds the open-ended tube surface of a Tube spline as a triangle mesh, rings laid out like the render sweep (same angles, Linear corners mitred).
 * Every quad is emitted with both windings so it collides from inside and outside. Appends to the out vectors.
 */
void BuildHollowSplineColliderMesh(const SplineParams& params, Core::Span<const SplineFrame> frames, Core::TlsfAllocator* scratch, Core::Vector<Vec3>& outPositions, Core::Vector<uint32_t>& outIndices);

/** True if the procedural type has an analytic collider builder (else the caller falls back to the render-mesh physics path). */
bool CanBuildProceduralCollider(const ProceduralParams& params);

/**
 * Builds an analytic collider for a procedural shape, matching the render generator's pivot/orientation. Sets `outKind` and fills exactly one payload:
 * ConvexHull writes `outPositions`; Compound writes `outPrimitives`. Returns false for types without an analytic builder.
 */
bool BuildProceduralCollider(const ProceduralParams& params, PhysicsColliderKind& outKind, Core::Vector<SplineColliderPrimitive>& outPrimitives, Core::Vector<Vec3>& outPositions);

/** Copies a BuildProceduralCollider result along the repeat offset; a ConvexHull result becomes a Compound of hulls. */
void ApplyProceduralRepeat(const ProceduralRepeat& repeat, PhysicsColliderKind& kind, Core::Vector<SplineColliderPrimitive>& primitives, Core::Vector<Vec3>& positions);

/**
 * Builds a Compound collider for extruded 3D text: one axis-aligned Box per rendered glyph, sized from the glyph's plane bounds (in the same EM space the render geometry uses) and extruded by the text depth. Whitespace/empty glyphs contribute no box but still advance the pen. Appends to `out`; @p font must stay resident for the call.
 */
bool BuildText3DColliderPrimitives(const Font& font, const Text3DParams& params, Core::Vector<SplineColliderPrimitive>& out);
} // Engine

#endif //WILL_ENGINE_COLLIDER_GENERATION_H
