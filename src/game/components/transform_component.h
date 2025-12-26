//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_TRANSFORM_COMPONENT_H
#define WILL_ENGINE_TRANSFORM_COMPONENT_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Game
{
struct TransformComponent
{
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

inline glm::mat4 GetMatrix(const TransformComponent& transform)
{
    return glm::translate(glm::mat4(1.0f), transform.translation) * mat4_cast(transform.rotation) * glm::scale(glm::mat4(1.0f), transform.scale);
}
}

#endif //WILL_ENGINE_TRANSFORM_COMPONENT_H
