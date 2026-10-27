//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PHYSICS_BODY_COMPONENT_H
#define WILL_ENGINE_PHYSICS_BODY_COMPONENT_H

#include "engine/core/registry.h"
#include "physics/body_handle.h"
#include "physics/body_user_data.h"

namespace Engine::Component
{
/** The body's user data owner is this entity (BodyUserData), set when the body is created. */
struct PhysicsBodyRuntime
{
    Physics::BodyHandle bodyID;

    static void OnDestroy(Engine::Registry& registry, entt::entity entity);
};

static_assert(sizeof(entt::entity) == sizeof(uint32_t), "body user data stores the entity in 32 bits");

inline uint64_t BodyUserDataFor(entt::entity entity)
{
    return Physics::BodyUserData::FromOwner(entt::to_integral(entity));
}

/** The entity that owns a body with this user data, or entt::null when it has none or that entity is gone. */
inline entt::entity EntityFromBodyUserData(const Engine::Registry& registry, uint64_t userData)
{
    if (!Physics::BodyUserData::HasOwner(userData)) { return entt::null; }
    const auto entity = static_cast<entt::entity>(Physics::BodyUserData::Owner(userData));
    return registry.Valid(entity) ? entity : entt::null;
}

/** The body component of an entity whose body exists, or nullptr. */
inline const PhysicsBodyRuntime* FindBody(const Engine::Registry& registry, entt::entity entity)
{
    const auto* body = entity != entt::null ? registry.TryGet<PhysicsBodyRuntime>(entity) : nullptr;
    return body && body->bodyID.IsValid() ? body : nullptr;
}
}

#endif //WILL_ENGINE_PHYSICS_BODY_COMPONENT_H
