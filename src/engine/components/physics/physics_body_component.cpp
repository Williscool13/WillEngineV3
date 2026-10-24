//
// Created by William on 2026-03-21.
//

#include "physics_body_component.h"

#include "engine/include/engine_context.h"
#include "engine/engine_api.h"

namespace Engine::Component
{
void PhysicsBodyRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& physics = registry.get<PhysicsBodyRuntime>(entity);

    if (physics.bodyID.IsValid()) {
        state->commandQueue.Push({.type = CommandType::BodyDestroy, .payload = {.bodyId = physics.bodyID.value}});
    }
}
}
