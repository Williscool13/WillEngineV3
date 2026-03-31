//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_TRANSFORM_H
#define WILL_ENGINE_TRANSFORM_H

#include "math.h"

namespace Core::Math
{
struct Transform
{
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] glm::mat4 GetMatrix() const { return glm::translate(Mat4(1.0f), translation) * mat4_cast(rotation) * glm::scale(Mat4(1.0f), scale); }

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