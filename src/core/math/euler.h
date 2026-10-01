//
// Created by William on 2026-10-01.
//

#ifndef WILL_ENGINE_EULER_H
#define WILL_ENGINE_EULER_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Core::Math
{
/** glm::eulerAngles in degrees. Yaw comes from atan2 rather than glm's asin, which is ~0.03 degrees off at +-90. */
inline glm::vec3 EulerDegrees(const glm::quat& q)
{
    glm::vec3 euler = glm::eulerAngles(q);
    const float sinYaw = -2.0f * (q.x * q.z - q.w * q.y);
    const float rollY = 2.0f * (q.x * q.y + q.w * q.z);
    const float rollX = q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z;
    euler.y = glm::atan(sinYaw, glm::sqrt(rollX * rollX + rollY * rollY));
    return glm::degrees(euler);
}
} // Core::Math

#endif //WILL_ENGINE_EULER_H
