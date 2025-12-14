//
// Created by William on 2025-11-06.
//

#include "free_camera.h"

#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "core/math/constants.h"
#include "core/time/time_frame.h"

namespace Game
{
FreeCamera::FreeCamera() : Camera() {}

FreeCamera::FreeCamera(glm::vec3 startingPosition, glm::vec3 startingLookPoint)
{
    transform.translation = startingPosition;
    glm::vec3 forward = glm::normalize(startingLookPoint - startingPosition);
    glm::vec3 right = glm::normalize(glm::cross(forward, WORLD_UP));
    glm::vec3 up = glm::cross(right, forward);
    glm::mat3 rotMatrix(right, up, -forward);
    transform.rotation = glm::quat_cast(rotMatrix);
}

void FreeCamera::Update(Core::EngineContext* ctx, Core::InputFrame* inputFrame, const Core::TimeFrame* timeFrame)
{
    if (!ctx->windowContext.bCursorHidden) {
        return;
    }

    glm::vec3 velocity{0.f};
    float verticalVelocity{0.f};

    if (inputFrame->GetKey(Key::D).down) {
        velocity.x += 1.0f;
    }
    if (inputFrame->GetKey(Key::A).down) {
        velocity.x -= 1.0f;
    }
    if (inputFrame->GetKey(Key::LCTRL).down) {
        verticalVelocity -= 1.0f;
    }
    if (inputFrame->GetKey(Key::SPACE).down) {
        verticalVelocity += 1.0f;
    }
    if (inputFrame->GetKey(Key::W).down) {
        velocity.z += 1.0f;
    }
    if (inputFrame->GetKey(Key::S).down) {
        velocity.z -= 1.0f;
    }

    if (inputFrame->GetKey(Key::RIGHTBRACKET).pressed) {
        speed += 1;
    }
    if (inputFrame->GetKey(Key::LEFTBRACKET).pressed) {
        speed -= 1;
    }
    speed = glm::clamp(speed, -2.0f, 3.0f);

    float scale = speed;
    if (scale <= 0) {
        scale -= 1;
    }
    const float currentSpeed = static_cast<float>(glm::pow(10, scale));

    velocity *= timeFrame->deltaTime * currentSpeed;
    verticalVelocity *= timeFrame->deltaTime * currentSpeed;

    const float yaw = glm::radians(-inputFrame->mouseXDelta / 10.0f);
    const float pitch = glm::radians(-inputFrame->mouseYDelta / 10.0f);

    const glm::quat currentRotation = transform.rotation;
    const glm::vec3 forward = currentRotation * glm::vec3(0.0f, 0.0f, -1.0f);
    const float currentPitch = std::asin(forward.y);

    const float newPitch = glm::clamp(currentPitch + pitch, glm::radians(-89.9f), glm::radians(89.9f));
    const float pitchDelta = newPitch - currentPitch;

    const glm::quat yawQuat = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitchQuat = glm::angleAxis(pitchDelta, glm::vec3(1.0f, 0.0f, 0.0f));

    glm::quat newRotation = yawQuat * currentRotation * pitchQuat;
    transform.rotation = glm::normalize(newRotation);

    const glm::vec3 right = transform.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 forwardDir = transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);

    glm::vec3 finalVelocity = right * velocity.x + forwardDir * velocity.z;
    finalVelocity += glm::vec3(0.0f, verticalVelocity, 0.0f);

    transform.translation += finalVelocity;
}
} // Game
