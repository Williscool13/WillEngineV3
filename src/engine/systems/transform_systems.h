//
// Created by William on 2026-10-28.
//

#ifndef WILL_ENGINE_TRANSFORM_SYSTEMS_H
#define WILL_ENGINE_TRANSFORM_SYSTEMS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
class SystemRegistry;

/**
 * Refreshes the hierarchy iteration order iff a topology change flagged it dirty; a no-op otherwise.
 * Sorting is structural: SortHierarchy runs it as an exclusive node ahead of the declared passes that walk the depth-sorted HierarchyComponent pool.
 */
void EnsureHierarchyOrder(EngineState* state, SystemRegistry& registry);

void SortHierarchy(EngineContext* ctx, EngineState* state, SystemRegistry& registry);

/** Tags the descendants of every DirtyTransformRequest entity so physics follows a moved parent; physics-owned dynamic children are left alone while playing. */
void PropagateDirtyTransforms(EngineContext* ctx, EngineState* state, SystemRegistry& registry);

/**
 * Resolves WorldTransformRuntime at game rate: DirtyTransformRequest entities and their children from local transforms, dynamic bodies from their simulated pose.
 * Tags changed entities with RenderTransformDirtyRequest, and rebuilds state->physicsDrivenTransforms for the ones whose render pose still interpolates.
 * @param ctx
 * @param state
 */
void ResolveWorldTransforms(EngineContext* ctx, EngineState* state, SystemRegistry& registry);
} // Engine

#endif //WILL_ENGINE_TRANSFORM_SYSTEMS_H
