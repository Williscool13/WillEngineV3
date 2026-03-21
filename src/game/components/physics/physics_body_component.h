//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PHYSICS_BODY_COMPONENT_H
#define WILL_ENGINE_PHYSICS_BODY_COMPONENT_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <entt/entt.hpp>

namespace Game::Component
{
struct PhysicsBodyComponent
{
    JPH::BodyID bodyID;

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void OnDestroy(entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_PHYSICS_BODY_COMPONENT_H
