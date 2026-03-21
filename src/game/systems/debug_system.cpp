//
// Created by William on 2025-12-22.
//

#include "debug_system.h"

#include <Jolt/Jolt.h>
#include <tracy/Tracy.hpp>

#include "core/include/engine_context.h"
#include "engine/engine_api.h"
#include "game/components/common_components.h"
#include "game/components/debug_components.h"
#include "../components/physics/physics_components.h"
#include "game/components/physics/physics_body_component.h"
#include "physics/physics_system.h"

namespace Game
{
void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
}

void DebugProcessPhysicsCollisions(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    state->registry.clear<Component::AntiGravityTag>();

    for (const auto& event : ctx->physicsSystem->GetCollisionEvents()) {
        entt::entity e1 = state->bodyToEntity.contains(event.body1) ? state->bodyToEntity[event.body1] : entt::null;
        entt::entity e2 = state->bodyToEntity.contains(event.body2) ? state->bodyToEntity[event.body2] : entt::null;
        if (e1 == entt::null || e2 == entt::null) continue;

        if (state->registry.all_of<Component::FloorTag>(e2))
            state->registry.emplace_or_replace<Component::AntiGravityTag>(e1);
        else if (state->registry.all_of<Component::FloorTag>(e1))
            state->registry.emplace_or_replace<Component::AntiGravityTag>(e2);
    }

    ctx->physicsSystem->ClearCollisionEvents();
    ctx->physicsSystem->ClearActivationEvents();
}

void DebugApplyGroundForces(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();
    auto view = state->registry.view<Component::AntiGravityTag, Component::PhysicsBodyComponent>();
    for (const auto& [entity, body] : view.each()) {
        bodyInterface.AddImpulse(body.bodyID, JPH::Vec3(0, 100.0f, 0));
    }
}

#ifndef PACKAGED_BUILD
void DebugRender(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
}
#endif
} // Game
