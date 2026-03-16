//
// Created by William on 2026-03-01.
//

#ifndef WILL_ENGINE_COMPONENT_INITIALIZATION_H
#define WILL_ENGINE_COMPONENT_INITIALIZATION_H
#include <entt/entt.hpp>

#include "../components/common_components.h"
#include "../components/physics_components.h"
#include "../components/render_components.h"

namespace Game
{
template<typename T>
bool CanAddComponent(const entt::registry& registry, entt::entity entity) { return true; }

template<> bool CanAddComponent<Component::StaticMeshComponent>(const entt::registry& registry, entt::entity entity);
template<> bool CanAddComponent<Component::ProceduralMeshComponent>(const entt::registry& registry, entt::entity entity);
template<> bool CanAddComponent<Component::SplineMeshComponent>(const entt::registry& registry, entt::entity entity);

template<typename T>
void OnComponentAdded(T& component, entt::registry& registry, entt::entity entity) {}

template<> void OnComponentAdded<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity);
template<> void OnComponentAdded<Component::ProceduralMeshComponent>(Component::ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity);
template<> void OnComponentAdded<Component::SplineMeshComponent>(Component::SplineMeshComponent& component, entt::registry& registry, entt::entity entity);
template<> void OnComponentAdded<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity);
template<> void OnComponentAdded<Component::StableIdComponent>(Component::StableIdComponent& component, entt::registry& registry, entt::entity entity);

template<typename T>
void OnComponentRemoved(T& component, entt::registry& registry, entt::entity entity) { registry.remove<T>(entity); }
template<> void OnComponentRemoved<Component::StableIdComponent>(Component::StableIdComponent& component, entt::registry& registry, entt::entity entity);
template<> void OnComponentRemoved<Component::StaticMeshComponent>(Component::StaticMeshComponent& component, entt::registry& registry, entt::entity entity);
template<> void OnComponentRemoved<Component::ProceduralMeshComponent>(Component::ProceduralMeshComponent& component, entt::registry& registry, entt::entity entity);
template<> void OnComponentRemoved<Component::SplineMeshComponent>(Component::SplineMeshComponent& component, entt::registry& registry, entt::entity entity);
template<> void OnComponentRemoved<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity);

template<typename T>
void OnPlayStart(T& component, entt::registry& registry, entt::entity entity) {}
template<> void OnPlayStart<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity);

template<typename T>
void OnPlayStop(T& component, entt::registry& registry, entt::entity entity) {}
template<> void OnPlayStop<Component::PhysicsBodyDesc>(Component::PhysicsBodyDesc& component, entt::registry& registry, entt::entity entity);

} // Game

#endif //WILL_ENGINE_COMPONENT_INITIALIZATION_H