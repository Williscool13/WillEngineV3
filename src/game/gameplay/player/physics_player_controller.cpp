//
// Created by William on 2026-03-21.
//

#include "physics_player_controller.h"

#include "engine/include/engine_context.h"
#include "core/input/input_frame.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "game/components/camera_components.h"
#include "game/components/core_components.h"
#include "game/input/game_actions.h"
#include "physics/physics_system.h"

namespace Game
{
void PhysicsPlayerController::Initialize(Engine::EngineState* gameState, Engine::EngineContext* ctx, glm::vec3 spawnPosition)
{
    character = std::make_unique<PhysicsCharacter>();
    character->Initialize(gameState, ctx, spawnPosition);

    cameraParams.sideOffset = 0;
}

void PhysicsPlayerController::Update(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    const float deltaTime = state->timeFrame->deltaTime;

    glm::vec3 moveInput{0.0f};
    bool jumpRequested = false;

    if (state->inputContext == Engine::InputContext::Gameplay) {
        const Core::ActionState& lookAction = state->input.GetActionState(Game::Actions::ACTION_LOOK);
        lookYaw += glm::radians(-lookAction.axis.x * lookSpeed) * deltaTime;
        lookPitch += glm::radians(-lookAction.axis.y * lookSpeed) * deltaTime;

        const Core::ActionState& gamepadLookAction = state->input.GetActionState(Game::Actions::ACTION_LOOK_GAMEPAD);
        lookYaw += glm::radians(-gamepadLookAction.axis.x * gamepadLookSpeed) * deltaTime;
        lookPitch += glm::radians(gamepadLookAction.axis.y * gamepadLookSpeed) * deltaTime;

        lookPitch = glm::clamp(lookPitch, glm::radians(-89.0f), glm::radians(89.0f));

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

    // Camera always updates (even when cursor released, so the view doesn't freeze)
    glm::vec3 characterPos = character->GetInterpolatedPosition();
    const float aspectRatio = state->projectConfig.ResolvedGameAspect(static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight));
    Core::ViewData viewData = Camera::ComputeOrbitCameraSwept(
        characterPos, lookYaw, lookPitch,
        cameraParams, aspectRatio,
        glm::radians(state->projectConfig.gameCameraFovDegrees), state->projectConfig.gameCameraNearPlane,
        deltaTime, cameraState, ctx->physicsSystem
    );

    auto cameraView = state->registry.view<Component::GameCameraTag, Component::CameraComponent, Component::TransformComponent>();
    for (auto camEntity : cameraView) {
        auto& camera = cameraView.get<Component::CameraComponent>(camEntity);
        auto& transform = cameraView.get<Component::TransformComponent>(camEntity);
        camera.currentViewData = viewData;
        // Mirror the pose into the transform (portals / editor eject read it)
        transform.translation = viewData.cameraPos;
        const glm::vec3 f = glm::normalize(viewData.cameraForward);
        glm::vec3 right = glm::cross(f, WORLD_UP);
        const float rightLen = glm::length(right);
        if (rightLen > 1e-4f) {
            right /= rightLen;
            const glm::vec3 up = glm::cross(right, f);
            transform.rotation = glm::normalize(glm::quat_cast(glm::mat3(right, up, -f)));
        }
    }
}

void PhysicsPlayerController::Shutdown(Physics::PhysicsSystem* physicsSystem)
{
    character->Shutdown(physicsSystem);
    character.reset();
}
} // Game
