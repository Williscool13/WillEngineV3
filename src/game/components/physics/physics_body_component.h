//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_BODY_COMPONENT_H
#define WILL_ENGINE_PHYSICS_BODY_COMPONENT_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

#include "entt/entt.hpp"

namespace Game
{
struct PhysicsBodyComponent
{
    JPH::BodyID bodyID;
};

void OnPhysicsBodyAdded(entt::registry& reg, entt::entity entity);

void OnPhysicsBodyRemoved(entt::registry& reg, entt::entity entity);
} // Game

#endif //WILL_ENGINE_PHYSICS_BODY_COMPONENT_H