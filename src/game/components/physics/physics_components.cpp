//
// Created by William on 2026-01-30.
//

#include "physics_components.h"

#include "engine/engine_api.h"

namespace Game::Component
{
void PhysicsBodyComponent::on_construct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::GameState*>();
    auto& physics = registry.get<PhysicsBodyComponent>(entity);
    state->bodyToEntity[physics.bodyID] = entity;
}
void PhysicsBodyComponent::on_destroy(entt::registry& registry, entt::entity entity)
{

    auto* state = registry.ctx().get<Engine::GameState*>();
    auto& physics = registry.get<PhysicsBodyComponent>(entity);
    state->bodyToEntity.erase(physics.bodyID);
}
}
