//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_TRANSFORM_H
#define WILL_ENGINE_TRANSFORM_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Core::Math
{
struct Transform
{
    glm::vec3 translation{};
    glm::quat rotation{};
    glm::vec3 scale{};

    [[nodiscard]] glm::mat4 GetMatrix() const { return glm::translate(glm::mat4(1.0f), translation) * mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale); }

    static const Transform IDENTITY;
};

inline const Transform Transform::IDENTITY{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f}
};
} // Math

using Core::Math::Transform;

#endif //WILL_ENGINE_TRANSFORM_H