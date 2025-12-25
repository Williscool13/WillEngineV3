//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_BODY_COMPONENT_H
#define WILL_ENGINE_PHYSICS_BODY_COMPONENT_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

namespace Game
{
struct PhysicsBodyComponent
{
    JPH::BodyID bodyID;
    glm::vec3 previousPosition{};
    glm::quat previousRotation{};
};
} // Game

#endif //WILL_ENGINE_PHYSICS_BODY_COMPONENT_H