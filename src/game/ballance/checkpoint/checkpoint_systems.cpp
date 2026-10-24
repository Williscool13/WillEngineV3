//
// Created by William on 2026-03-26.
//

#include "checkpoint_systems.h"

#include <glm/gtc/quaternion.hpp>

#include "engine/engine_api.h"
#include "engine/components/physics/physics_components.h"
#include "game/ballance/fwd_components.h"
#include "game/ballance/state.h"
#include "game/ballance/checkpoint/checkpoint_component.h"
#include "game/ballance/checkpoint/death_zone_component.h"

namespace Ballance
{
void ConnectCheckpointObservers(entt::registry& registry)
{
    registry.on_construct<Component::CheckpointComponent>().connect<&Component::CheckpointComponent::OnConstruct>();
}

void DisconnectCheckpointObservers(entt::registry& registry)
{
    registry.on_construct<Component::CheckpointComponent>().disconnect<&Component::CheckpointComponent::OnConstruct>();
}

void CheckpointUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    for (const auto& event : state->physics.resolvedAddedEvents) {
        entt::entity checkpointEntity = entt::null;
        if (event.e1 != entt::null && state->registry.all_of<Component::CheckpointComponent>(event.e1)) {
            checkpointEntity = event.e1;
        } else if (event.e2 != entt::null && state->registry.all_of<Component::CheckpointComponent>(event.e2)) {
            checkpointEntity = event.e2;
        }

        if (checkpointEntity != entt::null) {
            const auto& checkpoint = state->registry.get<Component::CheckpointComponent>(checkpointEntity);
            if (checkpoint.priority > state->scene.currentCheckpointPriority) {
                state->scene.currentCheckpointId = checkpoint.checkpointId;
                state->scene.currentCheckpointPriority = checkpoint.priority;
            }
        }
    }
}

void DeathZoneUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    PhysicsCharacter* character = ctx->GetGameState<State>()->playerController.GetCharacter();
    if (!character) { return; }

    const entt::entity playerEntity = character->GetEntity();
    if (playerEntity == entt::null) { return; }

    for (const auto& event : state->physics.resolvedAddedEvents) {
        bool playerInvolved = (event.e1 == playerEntity || event.e2 == playerEntity);
        if (!playerInvolved) { continue; }

        entt::entity otherEntity = (event.e1 == playerEntity) ? event.e2 : event.e1;
        if (otherEntity == entt::null) { continue; }
        if (!state->registry.all_of<Component::DeathZoneTag>(otherEntity)) { continue; }

        // Find the active checkpoint entity
        entt::entity checkpointEntity = entt::null;
        auto checkpointView = state->registry.view<Component::CheckpointComponent, Component::TransformComponent>();
        for (auto entity : checkpointView) {
            const auto& cp = checkpointView.get<Component::CheckpointComponent>(entity);
            if (cp.checkpointId == state->scene.currentCheckpointId) {
                checkpointEntity = entity;
                break;
            }
        }

        if (checkpointEntity == entt::null) { break; }

        const auto& checkpoint = state->registry.get<Component::CheckpointComponent>(checkpointEntity);
        const auto& checkpointTransform = state->registry.get<Component::TransformComponent>(checkpointEntity);

        auto& playerTransform = state->registry.get<Component::TransformComponent>(playerEntity);
        playerTransform.translation = checkpointTransform.translation + checkpoint.spawnOffset;
        playerTransform.rotation = glm::quat(glm::radians(checkpoint.spawnRotation));

        state->registry.emplace_or_replace<Component::TeleportPhysicsTransformRequest>(playerEntity);
        state->registry.emplace_or_replace<Component::SetVelocityRequest>(playerEntity);
        state->requests.RequestRenderReset(Core::RenderReset::Cut);

        break;
    }
}
} // Ballance
