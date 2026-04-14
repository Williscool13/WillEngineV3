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
    const Core::InputFrame* input = state->inputFrame;

    glm::vec3 moveInput{0.0f};
    bool jumpRequested = false;

    if (state->bGameCursorCaptured) {
        lookYaw += glm::radians(-input->mouseXDelta * lookSpeed);
        lookPitch += glm::radians(-input->mouseYDelta * lookSpeed);
        lookPitch = glm::clamp(lookPitch, glm::radians(-89.0f), glm::radians(89.0f));

        const glm::quat horizontalRotation = glm::angleAxis(lookYaw, WORLD_UP);
        const glm::vec3 forward = horizontalRotation * WORLD_FORWARD;
        const glm::vec3 right = horizontalRotation * WORLD_RIGHT;

        if (input->GetKey(Key::W).down) moveInput += forward;
        if (input->GetKey(Key::S).down) moveInput -= forward;
        if (input->GetKey(Key::D).down) moveInput += right;
        if (input->GetKey(Key::A).down) moveInput -= right;

        if (glm::length(moveInput) > 0.001f) {
            moveInput = glm::normalize(moveInput);
        }

        jumpRequested = input->GetKey(Key::SPACE).pressed;
    }

    character->Update(deltaTime, moveInput, jumpRequested, ctx->physicsSystem);

    // Camera always updates (even when cursor released, so the view doesn't freeze)
    glm::vec3 characterPos = character->GetInterpolatedPosition();
    const float aspectRatio = static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight);
    Core::ViewData viewData = Camera::ComputeOrbitCameraSwept(
        characterPos, lookYaw, lookPitch,
        cameraParams, aspectRatio, deltaTime,
        cameraState, ctx->physicsSystem
    );

    auto cameraView = state->registry.view<Component::GameCameraTag, Component::CameraComponent>();
    for (auto camEntity : cameraView) {
        auto& camera = cameraView.get<Component::CameraComponent>(camEntity);
        camera.currentViewData = viewData;
    }
}

void PhysicsPlayerController::Shutdown(Physics::PhysicsSystem* physicsSystem)
{
    character->Shutdown(physicsSystem);
    character.reset();
}
} // Game
