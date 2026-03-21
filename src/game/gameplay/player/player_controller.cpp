//
// Created by William on 2026-03-20.
//

#include "player_controller.h"

#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "game/components/camera_components.h"
#include "game/components/character_components.h"
#include "game/components/core_components.h"
#include "physics/physics_system.h"

namespace Game
{
void PlayerController::Initialize(Engine::GameState* gameState, Physics::PhysicsSystem* physicsSystem, glm::vec3 spawnPosition)
{
    character = std::make_unique<Character>();
    character->Initialize(gameState, physicsSystem, spawnPosition);
}

void PlayerController::Update(Core::EngineContext* ctx, Engine::GameState* state)
{
    const float deltaTime = state->timeFrame->deltaTime;
    const Core::InputFrame* input = state->inputFrame;

    // Look
    if (!ctx->bImguiMouseCaptured) {
        lookYaw += glm::radians(-input->mouseXDelta * lookSpeed);
        lookPitch += glm::radians(-input->mouseYDelta * lookSpeed);
        lookPitch = glm::clamp(lookPitch, glm::radians(-89.9f), glm::radians(89.9f));
    }

    // Movement input relative to look direction (horizontal only)
    glm::vec3 moveInput{0.0f};
    bool jumpRequested = false;

    if (!ctx->bImguiKeyboardCaptured) {
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
    // Orbit camera: behind and slightly to the right
    const glm::quat orbitRotation = glm::angleAxis(lookYaw, WORLD_UP) * glm::angleAxis(lookPitch, WORLD_RIGHT);
    const glm::vec3 orbitForward = orbitRotation * WORLD_FORWARD;
    const glm::vec3 orbitRight = orbitRotation * WORLD_RIGHT;

    const glm::vec3 focusPoint = transform.translation + glm::vec3(0.0f, 1.2f, 0.0f);
    const glm::vec3 cameraPos = focusPoint - orbitForward * orbitDistance + orbitRight * orbitSideOffset;
    const glm::vec3 cameraForward = glm::normalize(focusPoint - cameraPos);

    auto cameraView = state->registry.view<Component::GameCameraTag, Component::CameraComponent>();
    for (auto camEntity : cameraView) {
        auto& camera = cameraView.get<Component::CameraComponent>(camEntity);

        camera.currentViewData.cameraPos = cameraPos;
        camera.currentViewData.cameraLookAt = focusPoint;
        camera.currentViewData.cameraForward = cameraForward;
        camera.currentViewData.cameraUp = WORLD_UP;
        camera.currentViewData.aspectRatio = static_cast<float>(ctx->windowContext.viewportWidth) / static_cast<float>(ctx->windowContext.viewportHeight);
        camera.currentViewData.fovRadians = glm::radians(70.0f);
        camera.currentViewData.nearPlane = 0.1f;
        camera.currentViewData.farPlane = 100.0f;

        camera.currentViewData.view = glm::lookAt(camera.currentViewData.cameraPos, camera.currentViewData.cameraLookAt, camera.currentViewData.cameraUp);
        camera.currentViewData.proj = glm::perspective(camera.currentViewData.fovRadians, camera.currentViewData.aspectRatio, camera.currentViewData.farPlane, camera.currentViewData.nearPlane);
    }



    ctx->setCursorHiddenFn(true);
}

void PlayerController::Shutdown(Physics::PhysicsSystem* physicsSystem)
{
    character->Shutdown(physicsSystem);
    character.reset();
}
} // Game
