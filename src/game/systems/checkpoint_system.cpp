//
// Created by William on 2026-03-27.
//

#include "checkpoint_system.h"

#include "core/include/engine_context.h"
#include "engine/engine_api.h"
#include "game/components/gameplay/checkpoint_component.h"
#include "physics/physics_system.h"

namespace Game
{
void CheckpointUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    for (const auto& event : ctx->physicsSystem->GetCollisionEvents()) {
        entt::entity e1 = state->bodyToEntity.contains(event.body1) ? state->bodyToEntity.at(event.body1) : entt::null;
        entt::entity e2 = state->bodyToEntity.contains(event.body2) ? state->bodyToEntity.at(event.body2) : entt::null;

        entt::entity checkpointEntity = entt::null;
        if (e1 != entt::null && state->registry.all_of<Component::CheckpointComponent>(e1)) {
            checkpointEntity = e1;
        } else if (e2 != entt::null && state->registry.all_of<Component::CheckpointComponent>(e2)) {
            checkpointEntity = e2;
        }

        if (checkpointEntity != entt::null) {
            const auto& checkpoint = state->registry.get<Component::CheckpointComponent>(checkpointEntity);
            state->currentCheckpointId = checkpoint.checkpointId;
        }
    }
}
} // Game
