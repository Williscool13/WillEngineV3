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

        if (state->inputFrame->GetKey(Key::RIGHTBRACKET).down) {
            freeCam.moveSpeed += 1;
        }
        if (state->inputFrame->GetKey(Key::LEFTBRACKET).down) {
            freeCam.moveSpeed -= 1;
        }

        // todo: look speed modification

        velocity *= state->timeFrame->deltaTime * freeCam.moveSpeed;
        verticalVelocity *= state->timeFrame->deltaTime * freeCam.moveSpeed;

        const float yaw = glm::radians(-state->inputFrame->mouseXDelta * freeCam.lookSpeed);
        const float pitch = glm::radians(-state->inputFrame->mouseYDelta * freeCam.lookSpeed);

        const glm::quat currentRotation = transform.transform.rotation;
        const glm::vec3 forward = currentRotation * glm::vec3(0.0f, 0.0f, -1.0f);
        const float currentPitch = std::asin(forward.y);

        const float newPitch = glm::clamp(currentPitch + pitch, glm::radians(-89.9f), glm::radians(89.9f));
        const float pitchDelta = newPitch - currentPitch;

        const glm::quat yawQuat = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat pitchQuat = glm::angleAxis(pitchDelta, glm::vec3(1.0f, 0.0f, 0.0f));

        glm::quat newRotation = yawQuat * currentRotation * pitchQuat;
        transform.transform.rotation = glm::normalize(newRotation);

        const glm::vec3 right = transform.transform.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 forwardDir = transform.transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);

        glm::vec3 finalVelocity = right * velocity.x + forwardDir * velocity.z;
        finalVelocity += glm::vec3(0.0f, verticalVelocity, 0.0f);

        transform.transform.translation += finalVelocity;

        glm::mat4 worldMatrix = transform.transform.GetMatrix();
        camera.view = glm::inverse(worldMatrix);
        // todo: arrange this some other way, pass camera parameters if needed. Reversal should be transparent to the game
        camera.projection = glm::perspective(glm::radians(70.0f), 4.0f / 3.0f, 1000.0f, 0.1f);
    }
}
} // Game
