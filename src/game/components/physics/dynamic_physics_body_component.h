//
// Created by William on 2025-12-26.
//

#ifndef WILL_ENGINE_DYNAMIC_PHYSICS_BODY_COMPONENT_H
#define WILL_ENGINE_DYNAMIC_PHYSICS_BODY_COMPONENT_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Game
{
struct DynamicPhysicsBodyComponent
{
    glm::vec3 previousPosition{};
    glm::quat previousRotation{};
};
} // Game

#endif //WILL_ENGINE_DYNAMIC_PHYSICS_BODY_COMPONENT_H
