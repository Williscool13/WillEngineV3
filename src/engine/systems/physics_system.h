//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_SYSTEM_H
#define WILL_ENGINE_PHYSICS_SYSTEM_H

#include "core/containers/arena_fixed_vector.h"
#include "core/types/aabb.h"
#include "engine/core/registry.h"


namespace Core
{
struct FrameBuffer;
struct ViewFamily;
}

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Engine
{
void ConnectPhysicsObservers(Engine::Registry& registry);
void DisconnectPhysicsObservers(Engine::Registry& registry);
void PhysicsPreUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void PhysicsUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void ResolveCollisionEvents(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void MarkPhysicsTransformsDirty(Engine::EngineState* state, Engine::SystemRegistry& registry);
/** Entities whose bodies' broadphase bounds touch the world box, from the gameplay arena. */
Core::ArenaFixedVector<entt::entity> QueryEntitiesInBox(Engine::EngineContext* ctx, const Engine::EngineState* state, const AABB& box, Engine::SystemRegistry& registry);
void UpdatePhysicsEditor(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void DebugRenderPhysics(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
void PhysicsMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void PhysicsMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void PhysicsShapeCreationResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void PhysicsBodyCreationResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
} // Engine

#endif //WILL_ENGINE_PHYSICS_SYSTEM_H
