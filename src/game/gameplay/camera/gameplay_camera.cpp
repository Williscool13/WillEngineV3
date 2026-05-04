//
// Created by William on 2026-03-21.
//

#include "gameplay_camera.h"

#include <glm/gtc/quaternion.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

#include "core/math/constants.h"
#include "physics/physics_system.h"
#include "physics/layers/layer_interface.h"

namespace Game::Camera
{
class ExcludePlayerLayerFilter : public JPH::ObjectLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer) const override
    {
        return inLayer != Physics::Layers::PLAYER && inLayer != Physics::Layers::SENSOR;
    }
};
static Core::ViewData BuildViewData(glm::vec3 cameraPos, glm::vec3 focusPoint, float aspectRatio)
{
    glm::vec3 forward = glm::normalize(focusPoint - cameraPos);

    Core::ViewData vd{};
    vd.cameraPos = cameraPos;
    vd.cameraLookAt = focusPoint;
    vd.cameraForward = forward;
    vd.cameraUp = WORLD_UP;
    vd.aspectRatio = aspectRatio;
    vd.fovRadians = glm::radians(70.0f);
    vd.nearPlane = 0.1f;
    vd.farPlane = 100.0f;
    vd.view = glm::lookAt(vd.cameraPos, vd.cameraLookAt, vd.cameraUp);

    float tanHalfFov = glm::tan(vd.fovRadians * 0.5f);
    glm::mat4 proj(0.0f);
    proj[0][0] = 1.0f / (vd.aspectRatio * tanHalfFov);
    proj[1][1] = 1.0f / tanHalfFov;
    proj[2][3] = -1.0f;
    proj[3][2] = vd.nearPlane;
    vd.proj = proj;
    // glm::mat4 mat(0.0f);
    // mat[0][0] = 1.0f / (aspect * tanHalfFov);
    // mat[1][1] = 1.0f / tanHalfFov;
    // mat[2][3] = -1.0f;
    // mat[3][2] = near;
    // vd.proj = glm::infinitePerspective(vd.fovRadians, vd.aspectRatio, vd.farPlane);
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
    float aspectRatio)
{
    glm::vec3 focusPoint, direction, right;
    ComputeOrbitVectors(focusPosition, yaw, pitch, params, focusPoint, direction, right);

    glm::vec3 cameraPos = focusPoint - direction * params.distance + right * params.sideOffset;
    return BuildViewData(cameraPos, focusPoint, aspectRatio);
}

Core::ViewData ComputeOrbitCameraSwept(
    glm::vec3 focusPosition,
    float yaw,
    float pitch,
    const OrbitCameraParams& params,
    float aspectRatio,
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
        JPH::SphereShape probeShape(params.probeRadius);
        glm::vec3 castDir = -direction * params.distance + right * params.sideOffset;
        float castLength = glm::length(castDir);
        if (castLength > 0.001f) {
            glm::vec3 castDirNorm = castDir / castLength;

            JPH::RShapeCast shapeCast(
                &probeShape,
                JPH::Vec3::sReplicate(1.0f),
                JPH::RMat44::sTranslation(JPH::RVec3(focusPoint.x, focusPoint.y, focusPoint.z)),
                JPH::Vec3(castDirNorm.x, castDirNorm.y, castDirNorm.z) * castLength
            );

            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
            ExcludePlayerLayerFilter layerFilter;
            physicsSystem->GetPhysicsSystem().GetNarrowPhaseQuery().CastShape(
                shapeCast,
                {},
                JPH::RVec3::sZero(),
                collector,
                {},
                layerFilter
            );

            if (collector.HadHit()) {
                float hitDistance = collector.mHit.mFraction * castLength;
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
    return BuildViewData(cameraPos, focusPoint, aspectRatio);
}

Core::ViewData ComputeFixedFollowCamera(
    glm::vec3 targetPosition,
    glm::vec3 cameraOffset,
    float aspectRatio)
{
    glm::vec3 cameraPos = targetPosition + cameraOffset;
    return BuildViewData(cameraPos, targetPosition, aspectRatio);
}
} // Game::Camera
