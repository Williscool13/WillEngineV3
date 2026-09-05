//
// Created by William on 2026-03-21.
//

#include "physics_body_component.h"

#include "engine/core/hash.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "physics/physics_system.h"

namespace Engine::Component
{
void PhysicsBodyComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& physics = registry.get<PhysicsBodyComponent>(entity);
    state->physics.bodyToEntity[physics.bodyID] = entity;
}

void PhysicsBodyComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& physics = registry.get<PhysicsBodyComponent>(entity);

    if (!physics.bodyID.IsInvalid()) {
        state->commandQueue.Push({.type = CommandType::BodyDestroy, .payload = {.bodyId = physics.bodyID.GetIndexAndSequenceNumber()}});
    }

    state->physics.bodyToEntity.Remove(physics.bodyID);
}
}
