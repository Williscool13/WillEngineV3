//
// Created by William on 2026-03-26.
//

#include "gameplay_systems.h"

#include <tracy/Tracy.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/time/time_frame.h"
#include "engine/engine_api.h"
#include "game/components/core_components.h"
#include "game/components/gameplay/checkpoint_component.h"
#include "game/components/gameplay/death_zone_component.h"
#include "game/components/gameplay/path_mover_component.h"
#include "game/components/gameplay/rotate_in_place_component.h"
#include "game/components/physics/physics_components.h"
#include "game/gameplay/player/physics_player_controller.h"

namespace Game
{
void UpdatePathMovers(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    const float dt = state->timeFrame->deltaTime;

    auto view = state->registry.view<Component::PathMoverComponent, Component::TransformComponent>();
    for (auto entity : view) {
        auto& mover = view.get<Component::PathMoverComponent>(entity);
        auto& transform = view.get<Component::TransformComponent>(entity);

        if (mover.spline.points.Size() < 2) {
            continue;
        }

        if (mover.waitTimer > 0.0f) {
            mover.waitTimer -= dt;
            state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
            continue;
        }

        const auto segmentCount = mover.spline.SegmentCount();
        const int32_t segmentMax = segmentCount - 1;
        const auto pointCount = static_cast<int32_t>(mover.spline.points.Size());

        // Finished the wait
        if (mover.bIsWaiting) {
            switch (mover.loopMode) {
                case Component::PathLoopMode::Loop:
                    if (mover.currentSegment >= segmentMax + 1) {
                        mover.currentSegment = 0;
                    }
                    break;
                case Component::PathLoopMode::PingPong:
                    if (mover.currentSegment >= pointCount - 1) {
                        mover.direction = -1;
                    }
                    else if (mover.currentSegment <= 0) {
                        mover.direction = 1;
                    }
                    break;
                case Component::PathLoopMode::Once:
                default:
                    break;
            }
            mover.bIsWaiting = false;
        }

        int32_t targetSegment = mover.currentSegment + mover.direction;
        if (targetSegment >= pointCount) {
            targetSegment = 0;
        }
        else if (targetSegment < 0) {
            targetSegment = pointCount - 1;
        }

        glm::vec3 oldPos;
        glm::quat oldRot;
        Component::EvaluatePath(mover.spline, mover.pointSettings, mover.currentSegment, targetSegment, mover.progress, oldPos, oldRot);

        const float speed = (static_cast<size_t>(targetSegment) < mover.pointSettings.Size()) ? mover.pointSettings[targetSegment].speed : 1.0f;
        const float wait = (static_cast<size_t>(targetSegment) < mover.pointSettings.Size()) ? mover.pointSettings[targetSegment].waitTime : 0.0f;

        mover.progress += speed * dt;

        if (mover.progress > 1.0f) {
            mover.bIsWaiting = true;
            mover.waitTimer = wait;
            mover.currentSegment = targetSegment;
            mover.progress = 0.0f;
        }

        glm::vec3 newPos;
        glm::quat newRot;
        Component::EvaluatePath(mover.spline, mover.pointSettings, mover.currentSegment, targetSegment, mover.progress, newPos, newRot);

        const glm::quat anchorRot = transform.rotation * glm::inverse(oldRot);
        transform.translation += anchorRot * (newPos - oldPos);
        transform.rotation = anchorRot * newRot;
        state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
    }
}

void UpdateRotateInPlace(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    const float dt = state->timeFrame->deltaTime;

    auto view = state->registry.view<Component::RotateInPlaceComponent, Component::TransformComponent>();
    for (auto entity : view) {
        auto& rotator = view.get<Component::RotateInPlaceComponent>(entity);
        auto& transform = view.get<Component::TransformComponent>(entity);

        const float axisLen = glm::length(rotator.axis);
        if (axisLen < 1e-5f || rotator.speedDegrees == 0.0f) {
            continue;
        }

        const glm::vec3 axis = rotator.axis / axisLen;
        const glm::quat delta = glm::angleAxis(glm::radians(rotator.speedDegrees * dt), axis);

        if (rotator.bWorldSpace) {
            transform.rotation = glm::normalize(delta * transform.rotation);
        }
        else {
            transform.rotation = glm::normalize(transform.rotation * delta);
        }

        state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
    }
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
            if (checkpoint.priority > state->currentCheckpointPriority) {
                state->currentCheckpointId = checkpoint.checkpointId;
                state->currentCheckpointPriority = checkpoint.priority;
            }
        }
    }
}

void DeathZoneUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto* playerController = state->registry.ctx().find<PhysicsPlayerController>();
    if (!playerController) { return; }

    PhysicsCharacter* character = playerController->GetCharacter();
    if (!character) { return; }

    const entt::entity playerEntity = character->GetEntity();
    if (playerEntity == entt::null) { return; }

    for (const auto& event : state->physics.resolvedAddedEvents) {
        bool playerInvolved = (event.e1 == playerEntity || event.e2 == playerEntity);
        if (!playerInvolved) { continue; }

        entt::entity otherEntity = (event.e1 == playerEntity) ? event.e2 : event.e1;
        if (otherEntity == entt::null) { continue; }
        if (!state->registry.all_of<Component::DeathZoneComponent>(otherEntity)) { continue; }

        // Find the active checkpoint entity
        entt::entity checkpointEntity = entt::null;
        auto checkpointView = state->registry.view<Component::CheckpointComponent, Component::TransformComponent>();
        for (auto entity : checkpointView) {
            const auto& cp = checkpointView.get<Component::CheckpointComponent>(entity);
            if (cp.checkpointId == state->currentCheckpointId) {
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

        state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(playerEntity);
        state->registry.emplace_or_replace<Component::SetVelocityTag>(playerEntity);

        break;
    }
}
} // Game
