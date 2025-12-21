//
// Created by William on 2025-12-21.
//

#include "free_camera_component.h"

#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "core/time/time_frame.h"
#include "engine/engine_api.h"
#include "camera_component.h"
#include "../common/transform_component.h"
#include "core/math/constants.h"

namespace Game
{
void UpdateFreeCamera(Core::EngineContext* ctx, Engine::GameState* state)
{
    auto view = state->registry.view<FreeCameraComponent, CameraComponent, TransformComponent>();
    for (entt::entity entity : view) {
        const auto& [freeCam, camera, transform] = view.get(entity);

        if (!ctx->windowContext.bCursorHidden) {
            return;
        }

        glm::vec3 velocity{0.f};
        float verticalVelocity{0.f};

        if (state->inputFrame->GetKey(Key::D).down) {
            velocity.x += 1.0f;
        }
        if (state->inputFrame->GetKey(Key::A).down) {
            velocity.x -= 1.0f;
        }
        if (state->inputFrame->GetKey(Key::LCTRL).down) {
            verticalVelocity -= 1.0f;
        }
        if (state->inputFrame->GetKey(Key::SPACE).down) {
            verticalVelocity += 1.0f;
        }
        if (state->inputFrame->GetKey(Key::W).down) {
            velocity.z += 1.0f;
        }
        if (state->inputFrame->GetKey(Key::S).down) {
            velocity.z -= 1.0f;
        }

        if (state->inputFrame->GetKey(Key::MINUS).down) {
            freeCam.moveSpeed += 0.1f;
        }
        if (state->inputFrame->GetKey(Key::EQUALS).down) {
            freeCam.lookSpeed -= 0.1f;
        }
        if (state->inputFrame->GetKey(Key::RIGHTBRACKET).down) {
            freeCam.moveSpeed += 1;
        }
        if (state->inputFrame->GetKey(Key::LEFTBRACKET).down) {
            freeCam.moveSpeed -= 1;
        }

        freeCam.lookSpeed = glm::clamp(freeCam.lookSpeed, 0.1f, 1.0f);
        freeCam.moveSpeed = glm::clamp(freeCam.moveSpeed, 1.0f, 100.0f);

        const float scaledMoveSpeed = state->timeFrame->deltaTime * freeCam.moveSpeed;
        velocity *= scaledMoveSpeed;
        verticalVelocity *= scaledMoveSpeed;

        const float yaw = glm::radians(-state->inputFrame->mouseXDelta * freeCam.lookSpeed);
        const float pitch = glm::radians(-state->inputFrame->mouseYDelta * freeCam.lookSpeed);

        const glm::quat currentRotation = transform.transform.rotation;
        const glm::vec3 forward = currentRotation * WORLD_FORWARD;
        const float currentPitch = std::asin(forward.y);

        const float newPitch = glm::clamp(currentPitch + pitch, glm::radians(-89.9f), glm::radians(89.9f));
        const float pitchDelta = newPitch - currentPitch;

        const glm::quat yawQuat = glm::angleAxis(yaw, WORLD_UP);
        const glm::quat pitchQuat = glm::angleAxis(pitchDelta, WORLD_RIGHT);

        transform.transform.rotation = glm::normalize(yawQuat * currentRotation * pitchQuat);

        const glm::vec3 right = transform.transform.rotation * WORLD_RIGHT;
        const glm::vec3 forwardDir = transform.transform.rotation * WORLD_FORWARD;

        transform.transform.translation += right * velocity.x + forwardDir * velocity.z + WORLD_UP * verticalVelocity;

        camera.cameraPos = transform.transform.translation;
        camera.cameraLookAt = transform.transform.translation + forwardDir;
        camera.cameraUp = WORLD_UP;
        camera.aspectRatio = ctx->windowContext.windowWidth / ctx->windowContext.windowHeight;
        camera.fovRadians = glm::radians(90.0f);
        camera.nearPlane = 0.1f;
        camera.farPlane = 1000.0f;
    }
}
} // Game
