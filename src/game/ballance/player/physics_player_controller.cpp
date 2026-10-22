//
// Created by William on 2026-03-21.
//

#include "physics_player_controller.h"

#include "engine/include/engine_context.h"
#include "engine/input/input_frame.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "engine/components/camera_components.h"
#include "engine/components/core_components.h"
#include "engine/systems/camera_system.h"
#include "game/ballance/fwd_components.h"
#include "game/input/game_actions.h"
#include "physics/physics_world.h"

namespace Ballance
{
void PhysicsPlayerController::Initialize(Engine::EngineState* gameState, Engine::EngineContext* ctx, glm::vec3 spawnPosition)
{
    character.emplace();
    character->Initialize(gameState, ctx, spawnPosition);

    cameraParams.sideOffset = 0;
}

void PhysicsPlayerController::Update(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    const float deltaTime = state->timeFrame->deltaTime;

    glm::vec3 moveInput{0.0f};
    bool jumpRequested = false;

    const Engine::CameraOverride& camOverride = state->playtest.cameraOverride;
    const bool bOverride = camOverride.mode != Engine::CameraOverride::Mode::None;

    if (state->inputContext == Engine::InputContext::Gameplay) {
        if (!bOverride) {
            const Core::ActionState& lookAction = state->input.GetActionState(Game::Actions::ACTION_LOOK);
            lookYaw += glm::radians(-lookAction.axis.x * lookSpeed) * deltaTime;
            lookPitch += glm::radians(-lookAction.axis.y * lookSpeed) * deltaTime;

            const Core::ActionState& gamepadLookAction = state->input.GetActionState(Game::Actions::ACTION_LOOK_GAMEPAD);
            lookYaw += glm::radians(-gamepadLookAction.axis.x * gamepadLookSpeed) * deltaTime;
            lookPitch += glm::radians(gamepadLookAction.axis.y * gamepadLookSpeed) * deltaTime;

            lookPitch = glm::clamp(lookPitch, glm::radians(-89.0f), glm::radians(89.0f));
        }

        const glm::quat horizontalRotation = glm::angleAxis(lookYaw, WORLD_UP);
        glm::vec3 forward = horizontalRotation * WORLD_FORWARD;
        if (bOverride) {
            glm::vec3 overrideForward = camOverride.mode == Engine::CameraOverride::Mode::Held ? camOverride.rotation * WORLD_FORWARD : character->GetInterpolatedPosition() - camOverride.translation;
            overrideForward.y = 0.0f;
            if (glm::length(overrideForward) > DIRECTION_EPSILON) {
                forward = glm::normalize(overrideForward);
            }
        }
        const glm::vec3 right = glm::cross(forward, WORLD_UP);

        const Core::ActionState& moveAction = state->input.GetActionState(Game::Actions::ACTION_MOVE);
        moveInput += forward * moveAction.axis.y;
        moveInput += right * moveAction.axis.x;

        if (glm::length(moveInput) > 0.001f) {
            moveInput = glm::normalize(moveInput);
        }

        jumpRequested = state->input.GetActionState(Game::Actions::ACTION_JUMP).pressed;
    }

    character->Update(deltaTime, moveInput, jumpRequested, ctx->physicsWorld);

    // Camera always updates (even when cursor released, so the view doesn't freeze)
    glm::vec3 characterPos = character->GetInterpolatedPosition();
    const float aspectRatio = state->projectConfig.ResolvedGameAspect(static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight));
    const float fov = glm::radians(state->projectConfig.gameCameraFovDegrees);
    const float nearPlane = state->projectConfig.gameCameraNearPlane;
    Core::ViewData viewData{};
    if (camOverride.mode == Engine::CameraOverride::Mode::Held) {
        viewData = Engine::BuildPerspectiveView(camOverride.translation, camOverride.rotation * WORLD_FORWARD, WORLD_UP, aspectRatio, fov, nearPlane);
    }
    else if (camOverride.mode == Engine::CameraOverride::Mode::Track) {
        const glm::vec3 toPlayer = characterPos - camOverride.translation;
        const glm::vec3 forward = glm::length(toPlayer) > POSITION_EPSILON ? glm::normalize(toPlayer) : camOverride.rotation * WORLD_FORWARD;
        viewData = Engine::BuildPerspectiveView(camOverride.translation, forward, WORLD_UP, aspectRatio, fov, nearPlane);
    }
    else {
        viewData = Camera::ComputeOrbitCameraSwept(
            characterPos, lookYaw, lookPitch,
            cameraParams, aspectRatio,
            fov, nearPlane,
            deltaTime, cameraState, ctx->physicsWorld
        );
    }

    auto cameraView = state->registry.view<Component::GameCameraTag, Component::CameraComponent, Component::TransformComponent>();
    for (auto camEntity : cameraView) {
        auto& camera = cameraView.get<Component::CameraComponent>(camEntity);
        auto& transform = cameraView.get<Component::TransformComponent>(camEntity);
        camera.currentViewData = viewData;
        // Mirror the pose into the transform (editor eject reads it)
        transform.translation = viewData.cameraPos;
        const glm::vec3 f = glm::normalize(viewData.cameraForward);
        glm::vec3 right = glm::cross(f, WORLD_UP);
        const float rightLen = glm::length(right);
        if (rightLen > DIRECTION_EPSILON) {
            right /= rightLen;
            const glm::vec3 up = glm::cross(right, f);
            transform.rotation = glm::normalize(glm::quat_cast(glm::mat3(right, up, -f)));
        }
    }
}

void PhysicsPlayerController::Shutdown(Physics::PhysicsWorld* physicsWorld)
{
    character->Shutdown(physicsWorld);
    character.reset();
}
} // Ballance
