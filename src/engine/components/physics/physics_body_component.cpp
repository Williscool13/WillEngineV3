//
// Created by William on 2026-03-21.
//

#include "physics_body_component.h"

#include "engine/include/engine_context.h"
#include "engine/engine_api.h"

namespace Engine::Component
{
void PhysicsBodyRuntime::OnDestroy(Engine::SystemRegistry& registry, entt::entity entity)
{
    const auto& physics = registry.Get<PhysicsBodyRuntime>(entity);

    if (physics.bodyID.IsValid()) {
        registry.PushCommand({.type = CommandType::BodyDestroy, .payload = {.bodyId = physics.bodyID.value}});
    }
}
}
