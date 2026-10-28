//
// Created by William on 2026-10-17.
//

#ifndef WILL_ENGINE_BALLANCE_BREAKABLE_SYSTEMS_H
#define WILL_ENGINE_BALLANCE_BREAKABLE_SYSTEMS_H

#include "engine/core/registry.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Ballance
{
void RegisterBreakableCommands(Engine::EngineState* state);

void ConnectBreakableObservers(Engine::Registry& registry);
void DisconnectBreakableObservers(Engine::Registry& registry);

/** Gives each BreakableComponent a BreakableRuntime holding its broken model and per-node hulls once they can be loaded. */
void AcquireBreakableAssets(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void UpdateSelfDestruct(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void ShatterOnContact(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

/**
 * Spawns the entity's broken model as one dynamic shard per mesh node, each moving with the entity's velocity at its centre of mass, and destroys the entity at the end of the frame.
 * The shards spawn through RunExclusive; breaking an entity twice before they do spawns them once.
 * @return false when the entity has no BreakableComponent or its broken model is not loaded yet
 */
bool Break(Engine::EngineContext* ctx, entt::entity entity, Engine::SystemRegistry& registry);
} // Ballance

#endif //WILL_ENGINE_BALLANCE_BREAKABLE_SYSTEMS_H
