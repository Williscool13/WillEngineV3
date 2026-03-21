//
// Created by William on 2026-03-01.
//

#ifndef WILL_ENGINE_COMPONENT_INITIALIZATION_H
#define WILL_ENGINE_COMPONENT_INITIALIZATION_H
#include <entt/entt.hpp>

#include "../components/common_components.h"
#include "../components/common/stable_id_component.h"
#include "../components/physics/physics_components.h"
#include "../components/render_components.h"
#include "../components/render/static_mesh_component.h"

namespace Game
{
template<typename T>
bool CanAddComponent(const entt::registry& registry, entt::entity entity) { return true; }

template<> bool CanAddComponent<Component::StaticMeshComponent>(const entt::registry& registry, entt::entity entity);
template<> bool CanAddComponent<Component::ProceduralMeshComponent>(const entt::registry& registry, entt::entity entity);
template<> bool CanAddComponent<Component::SplineMeshComponent>(const entt::registry& registry, entt::entity entity);

} // Game

#endif //WILL_ENGINE_COMPONENT_INITIALIZATION_H