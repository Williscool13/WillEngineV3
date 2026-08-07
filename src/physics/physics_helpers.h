//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_PHYSICS_HELPERS_H
#define WILL_ENGINE_PHYSICS_HELPERS_H

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Mat44.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Jolt/Core/Color.h"

namespace Physics::PhysicsHelpers
{
inline glm::vec3 ToGLM(JPH::Vec3Arg v)
{
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

inline glm::quat ToGLM(JPH::QuatArg q)
{
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

inline glm::mat4 ToGLM(JPH::Mat44Arg m)
{
    glm::mat4 out;
    m.StoreFloat4x4(reinterpret_cast<JPH::Float4*>(&out));
    return out;
}

inline glm::vec4 ToGLM(JPH::ColorArg c)
{
    return glm::vec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

inline JPH::Vec3 ToJolt(const glm::vec3& v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

inline JPH::RVec3 ToJoltR(const glm::vec3& v)
{
    return JPH::RVec3(v.x, v.y, v.z);
}

inline JPH::Quat ToJolt(const glm::quat& q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

inline JPH::Mat44 ToJolt(const glm::mat4& m)
{
    return JPH::Mat44::sLoadFloat4x4(reinterpret_cast<const JPH::Float4*>(&m));
}

inline JPH::Color ToJolt(const glm::vec4& c)
{
    return JPH::Color(
        static_cast<uint8_t>(c.r * 255.0f),
        static_cast<uint8_t>(c.g * 255.0f),
        static_cast<uint8_t>(c.b * 255.0f),
        static_cast<uint8_t>(c.a * 255.0f)
    );
}
} // Physics::PhysicsHelpers

#endif // WILL_ENGINE_PHYSICS_HELPERS_H
