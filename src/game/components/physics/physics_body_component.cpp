//
// Created by William on 2026-03-21.
//

#include "physics_body_component.h"

#include "engine/core/hash.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "physics/physics_system.h"

namespace Game::Component
{
void PhysicsBodyComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& physics = registry.get<PhysicsBodyComponent>(entity);
    state->bodyToEntity[physics.bodyID] = entity;
}

void PhysicsBodyComponent::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto* ctx = registry.ctx().get<Engine::EngineContext*>();
    auto& physics = registry.get<PhysicsBodyComponent>(entity);

    if (!physics.bodyID.IsInvalid()) {
        JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();
        bodyInterface.RemoveBody(physics.bodyID);
        bodyInterface.DestroyBody(physics.bodyID);
    }

    state->bodyToEntity.Remove(physics.bodyID);
}
}
