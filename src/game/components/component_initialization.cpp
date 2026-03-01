//
// Created by William on 2026-03-01.
//

#include "component_initialization.h"

#include "engine/logging/engine_log.h"

namespace Game
{
template<>
void OnComponentAdded<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity)
{
    LOG_INFO(Game, "Test");
}
} // Game