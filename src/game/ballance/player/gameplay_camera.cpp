//
// Created by William on 2026-03-21.
//

#include "gameplay_camera.h"

#include <glm/gtc/quaternion.hpp>

#include "core/math/constants.h"
#include "engine/systems/camera_system.h"
#include "physics/physics_system.h"
#include "physics/layers/layer_interface.h"

namespace Ballance::Camera
{
static Core::ViewData BuildViewData(glm::vec3 cameraPos, glm::vec3 focusPoint, float aspectRatio, float fovRadians, float nearPlane)
{
    Core::ViewData vd = Engine::BuildPerspectiveView(cameraPos, focusPoint - cameraPos, WORLD_UP, aspectRatio, fovRadians, nearPlane);
    vd.cameraLookAt = focusPoint;
    return vd;
}

static void ComputeOrbitVectors(glm::vec3 focusPosition, float yaw, float pitch, const OrbitCameraParams& params,
                                glm::vec3& outFocusPoint, glm::vec3& outDirection, glm::vec3& outRight)
{
    constexpr float pitchLimit = glm::radians(89.0f);
    float clampedPitch = glm::clamp(pitch, -pitchLimit, pitchLimit);
    const glm::quat orbitRotation = glm::angleAxis(yaw, WORLD_UP) * glm::angleAxis(clampedPitch, WORLD_RIGHT);
    outDirection = orbitRotation * WORLD_FORWARD;
    outRight = orbitRotation * WORLD_RIGHT;
    outFocusPoint = focusPosition + glm::vec3(0.0f, params.focusHeight, 0.0f);
}

Core::ViewData ComputeOrbitCamera(
    glm::vec3 focusPosition,
    float yaw,
    float pitch,
    const OrbitCameraParams& params,
    float aspectRatio,
    float fovRadians,
    float nearPlane)
{
    glm::vec3 focusPoint, direction, right;
    ComputeOrbitVectors(focusPosition, yaw, pitch, params, focusPoint, direction, right);

    glm::vec3 cameraPos = focusPoint - direction * params.distance + right * params.sideOffset;
    return BuildViewData(cameraPos, focusPoint, aspectRatio, fovRadians, nearPlane);
}

Core::ViewData ComputeOrbitCameraSwept(
    glm::vec3 focusPosition,
    float yaw,
    float pitch,
    const OrbitCameraParams& params,
    float aspectRatio,
    float fovRadians,
    float nearPlane,
    float deltaTime,
    OrbitCameraState& state,
    Physics::PhysicsSystem* physicsSystem)
{
    if (!state.initialized) {
        state.currentDistance = params.distance;
        state.initialized = true;
    }

    glm::vec3 focusPoint, direction, right;
    ComputeOrbitVectors(focusPosition, yaw, pitch, params, focusPoint, direction, right);

    float allowedDistance = params.distance;

    if (physicsSystem) {
        // Sphere cast from focus point toward desired camera position
        glm::vec3 castDir = -direction * params.distance + right * params.sideOffset;
        float castLength = glm::length(castDir);
        if (castLength > 0.001f) {
            constexpr uint32_t IGNORED_LAYERS = Physics::Layers::Bit(Physics::Layers::PLAYER) | Physics::Layers::Bit(Physics::Layers::SENSOR) | Physics::Layers::Bit(Physics::Layers::DEBRIS);
            Physics::CastHit hit;
            if (physicsSystem->CastSphere(params.probeRadius, focusPoint, castDir, hit, {}, IGNORED_LAYERS)) {
                float hitDistance = hit.fraction * castLength;
                // Map back to orbit distance (approximate, since sideOffset skews the cast)
                allowedDistance = glm::max(hitDistance * (params.distance / castLength), params.minDistance);
            }
        }
    }

    // Snap in fast, ease out slow
    if (allowedDistance < state.currentDistance) {
        state.currentDistance = glm::mix(state.currentDistance, allowedDistance, glm::min(1.0f, params.pullInSpeed * deltaTime));
    }
    else {
        state.currentDistance = glm::mix(state.currentDistance, allowedDistance, glm::min(1.0f, params.pushOutSpeed * deltaTime));
    }

    glm::vec3 cameraPos = focusPoint - direction * state.currentDistance + right * params.sideOffset;
    return BuildViewData(cameraPos, focusPoint, aspectRatio, fovRadians, nearPlane);
}

Core::ViewData ComputeFixedFollowCamera(
    glm::vec3 targetPosition,
    glm::vec3 cameraOffset,
    float aspectRatio,
    float fovRadians,
    float nearPlane)
{
    glm::vec3 cameraPos = targetPosition + cameraOffset;
    return BuildViewData(cameraPos, targetPosition, aspectRatio, fovRadians, nearPlane);
}
} // Ballance::Camera
