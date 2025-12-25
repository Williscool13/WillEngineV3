//
// Created by William on 2025-12-25.
//

#include "physics_system.h"
#include "physics/physics_system.h"

#include "core/include/engine_context.h"
#include "core/time/time_frame.h"
#include "engine/engine_api.h"

namespace Game::System
{
void UpdatePhysics(Core::EngineContext* ctx, Engine::GameState* state)
{
    auto* physics = ctx->physicsSystem;

    state->physicsDeltaTimeAccumulator += state->timeFrame->deltaTime;

    while (state->physicsDeltaTimeAccumulator >= Physics::PHYSICS_TIMESTEP) {
        physics->Step(Physics::PHYSICS_TIMESTEP);
        state->physicsDeltaTimeAccumulator -= Physics::PHYSICS_TIMESTEP;
    }

    for (auto& event : physics->GetCollisionEvents()) {
        // HandleCollision(state->registry, event);
    }
    for (auto& activation : physics->GetActivatedEvents()) {
        // HandleActivation(state->registry, activation);
    }
    for (auto& deactivation : physics->GetDeactivatedEvents()) {
        // HandleDeactivation(state->register, deactivation);
    }
    physics->ClearEvents();
}
} // Game
