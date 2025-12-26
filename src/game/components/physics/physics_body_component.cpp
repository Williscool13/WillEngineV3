//
// Created by William on 2025-12-26.
//

#include "physics_body_component.h"

#include "engine/engine_api.h"

namespace Game
{
void OnPhysicsBodyAdded(entt::registry& reg, entt::entity entity)
{
    auto* state = reg.ctx().get<Engine::GameState*>();
    auto& physics = reg.get<PhysicsBodyComponent>(entity);
    state->bodyToEntity[physics.bodyID] = entity;
}

void OnPhysicsBodyRemoved(entt::registry& reg, entt::entity entity)
{
    auto* state = reg.ctx().get<Engine::GameState*>();
    auto& physics = reg.get<PhysicsBodyComponent>(entity);
    state->bodyToEntity.erase(physics.bodyID);
}
}
