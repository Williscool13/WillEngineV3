//
// Created by William on 2026-03-01.
//

#ifndef WILL_ENGINE_COMPONENT_INITIALIZATION_H
#define WILL_ENGINE_COMPONENT_INITIALIZATION_H
#include <entt/entt.hpp>

#include "render_components.h"

namespace Game
{
template<typename T>
void OnComponentAdded(T& component, entt::registry& registry, entt::entity entity) {}

template<> void OnComponentAdded<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity);

} // Game

#endif //WILL_ENGINE_COMPONENT_INITIALIZATION_H