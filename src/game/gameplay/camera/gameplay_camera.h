//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_GAMEPLAY_CAMERA_H
#define WILL_ENGINE_GAMEPLAY_CAMERA_H

#include <glm/glm.hpp>
#include "../../../render/interface/render_interface.h"

namespace Physics
{
class PhysicsSystem;
}

namespace Game::Camera
{
struct OrbitCameraParams
{
    float distance{3.5f};
    float sideOffset{0.5f};
    float focusHeight{1.2f};

    float probeRadius{0.15f};
    float pullInSpeed{20.0f};
    float pushOutSpeed{4.0f};
    float minDistance{0.5f};
};

struct OrbitCameraState
{
    float currentDistance{0.0f};
    bool initialized{false};
};

Core::ViewData ComputeOrbitCamera(
    glm::vec3 focusPosition,
    float yaw,
    float pitch,
    const OrbitCameraParams& params,
    float aspectRatio
);

Core::ViewData ComputeOrbitCameraSwept(
    glm::vec3 focusPosition,
    float yaw,
    float pitch,
    const OrbitCameraParams& params,
    float aspectRatio,
    float deltaTime,
    OrbitCameraState& state,
    Physics::PhysicsSystem* physicsSystem
);

Core::ViewData ComputeFixedFollowCamera(
    glm::vec3 targetPosition,
    glm::vec3 cameraOffset,
    float aspectRatio
);
} // Game::Camera

#endif //WILL_ENGINE_GAMEPLAY_CAMERA_H
