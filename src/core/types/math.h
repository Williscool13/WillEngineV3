//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_MATH_H
#define WILL_ENGINE_MATH_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Core::Math
{
    using Vec2  = glm::vec2;
    using Vec3  = glm::vec3;
    using Vec4  = glm::vec4;
    using Mat2  = glm::mat2;
    using Mat3  = glm::mat3;
    using Mat4  = glm::mat4;
    using Quat  = glm::quat;
    using IVec2 = glm::ivec2;
    using IVec3 = glm::ivec3;
    using IVec4 = glm::ivec4;
    using UVec2 = glm::uvec2;
    using UVec3 = glm::uvec3;
    using UVec4 = glm::uvec4;
} // namespace Core::Math

using Core::Math::Vec2;
using Core::Math::Vec3;
using Core::Math::Vec4;
using Core::Math::Mat2;
using Core::Math::Mat3;
using Core::Math::Mat4;
using Core::Math::Quat;
using Core::Math::IVec2;
using Core::Math::IVec3;
using Core::Math::IVec4;
using Core::Math::UVec2;
using Core::Math::UVec3;
using Core::Math::UVec4;

#endif // WILL_ENGINE_MATH_H
