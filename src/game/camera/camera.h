//
// Created by William on 2025-11-06.
//

#ifndef WILLENGINETESTBED_CAMERA_H
#define WILLENGINETESTBED_CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/transform.h"

namespace Core
{
struct EngineContext;
struct InputFrame;
struct TimeFrame;
}

namespace Game
{
class Camera
{
public:
    virtual ~Camera() = default;

    virtual void Update(Core::EngineContext* ctx, Core::InputFrame* inputFrame, const Core::TimeFrame* timeFrame) = 0;

    // Game thread provides these
    glm::vec3 GetPosition() const { return transform.translation; }
    glm::quat GetRotation() const { return transform.rotation; }
    glm::vec3 GetForward() const { return transform.rotation * glm::vec3(0, 0, -1); }
    glm::vec3 GetRight() const { return transform.rotation * glm::vec3(1, 0, 0); }
    glm::vec3 GetUp() const { return transform.rotation * glm::vec3(0, 1, 0); }

    float GetFov() const { return fov; }
    float GetNearPlane() const { return nearPlane; }
    float GetFarPlane() const { return farPlane; }

    void SetFov(const float f) { fov = f; }

    void SetPlanes(float near, float far);

public:
    Transform transform{Transform::IDENTITY};
    float fov = glm::radians(75.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};
} // Game

#endif //WILLENGINETESTBED_CAMERA_H
