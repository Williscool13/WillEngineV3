//
// Created by William on 2026-03-20.
//

#include "player_controller.h"

#include "engine/include/engine_context.h"
#include "core/input/input_frame.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "game/components/camera_components.h"
#include "game/components/character_components.h"
#include "game/components/core_components.h"
#include "game/input/game_actions.h"
#include "physics/physics_system.h"

namespace Game
{
void PlayerController::Initialize(Engine::EngineState* gameState, Physics::PhysicsSystem* physicsSystem, glm::vec3 spawnPosition)
{
    character = std::make_unique<Character>();
    character->Initialize(gameState, physicsSystem, spawnPosition);
}

void PlayerController::Update(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    const float deltaTime = state->timeFrame->deltaTime;

    glm::vec3 moveInput{0.0f};
    bool jumpRequested = false;

    if (state->inputContext == Engine::InputContext::Gameplay) {
        const Core::ActionState& lookAction = state->input.GetActionState(Game::Actions::ACTION_LOOK);
        lookYaw += glm::radians(-lookAction.axis.x * lookSpeed);
        lookPitch += glm::radians(-lookAction.axis.y * lookSpeed);
        lookPitch = glm::clamp(lookPitch, glm::radians(-89.9f), glm::radians(89.9f));

        const glm::quat horizontalRotation = glm::angleAxis(lookYaw, WORLD_UP);
        const glm::vec3 forward = horizontalRotation * WORLD_FORWARD;
        const glm::vec3 right = horizontalRotation * WORLD_RIGHT;

        const Core::ActionState& moveAction = state->input.GetActionState(Game::Actions::ACTION_MOVE);
        moveInput += forward * moveAction.axis.y;
        moveInput += right * moveAction.axis.x;

        if (glm::length(moveInput) > 0.001f) {
            moveInput = glm::normalize(moveInput);
        }

        jumpRequested = state->input.GetActionState(Game::Actions::ACTION_JUMP).pressed;
    }

    character->Update(deltaTime, moveInput, jumpRequested, ctx->physicsSystem);

    // Sync physics position back to transform
    auto& comp = state->registry.get<Component::CharacterPhysicsComponent>(character->GetEntity());
    JPH::RVec3 pos = comp.character->GetPosition();
    auto& transform = state->registry.get<Component::TransformComponent>(character->GetEntity());
    transform.translation = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
    if (glm::length(moveInput) > 0.001f) {
        float targetYaw = std::atan2(-moveInput.x, -moveInput.z);
        characterYaw = targetYaw;
    }
    transform.rotation = glm::angleAxis(characterYaw, WORLD_UP);
    state->registry.emplace_or_replace<Component::DirtyTransformTag>(character->GetEntity());

    // Camera
    const float aspectRatio = state->projectConfig.ResolvedGameAspect(static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight));
    Core::ViewData viewData = Camera::ComputeOrbitCameraSwept(
        transform.translation, lookYaw, lookPitch,
        cameraParams, aspectRatio,
        glm::radians(state->projectConfig.gameCameraFovDegrees), state->projectConfig.gameCameraNearPlane,
        deltaTime, cameraState, ctx->physicsSystem
    );

    auto cameraView = state->registry.view<Component::GameCameraTag, Component::CameraComponent>();
    for (auto camEntity : cameraView) {
        auto& camera = cameraView.get<Component::CameraComponent>(camEntity);
        camera.currentViewData = viewData;
    }
}

void PlayerController::Shutdown(Physics::PhysicsSystem* physicsSystem)
{
    character->Shutdown(physicsSystem);
    character.reset();
}
} // Game
