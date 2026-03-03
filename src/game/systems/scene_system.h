//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_SYSTEM_H
#define WILL_ENGINE_SCENE_SYSTEM_H
#include <entt/entt.hpp>

#include "game/scene/scene.h"
#include "game/components/component_registry.h"
#include "core/string_id.h"
#include "engine/engine_api.h"

namespace Core
{
struct EngineContext;
}

namespace Engine
{
struct GameState;
class AssetManager;
}

namespace Game
{
Scene SaveScene(ComponentRegistry& componentRegistry, entt::registry& registry, StringID sceneId, std::string_view sceneName);

std::string LoadScene(ComponentRegistry& componentRegistry, entt::registry& registry, Scene& scene);

entt::entity CreateSceneEntity(Engine::GameState* state);
template<typename T>
T& CreateComponent(Engine::GameState* state, entt::entity entity)
{
    auto it = state->componentRegistry.registryMapping.find(TypeSID<T>());
    assert(it != state->componentRegistry.registryMapping.end() && "Component type not registered");
    ComponentEntry& entry = state->componentRegistry.registry[it->second];
    entry.onAddComponent(state->registry, entity);
    return state->registry.get<T>(entity);
}
template<typename T>
void DestroyComponent(Engine::GameState* state, entt::entity entity)
{
    auto it = state->componentRegistry.registryMapping.find(TypeSID<T>());
    if (it == state->componentRegistry.registryMapping.end()) return;
    ComponentEntry& entry = state->componentRegistry.registry[it->second];
    entry.onRemoveComponent(state->registry, entity);
    state->registry.remove<T>(entity);
}

inline void CreateComponent(Engine::GameState* state, entt::entity entity, StringID typeId)
{
    auto it = state->componentRegistry.registryMapping.find(typeId);
    assert(it != state->componentRegistry.registryMapping.end() && "Component type not registered");
    ComponentEntry& entry = state->componentRegistry.registry[it->second];
    entry.onAddComponent(state->registry, entity);
}

inline void DestroyComponent(Engine::GameState* state, entt::entity entity, StringID typeId)
{
    auto it = state->componentRegistry.registryMapping.find(typeId);
    assert(it != state->componentRegistry.registryMapping.end() && "Component type not registered");
    ComponentEntry& entry = state->componentRegistry.registry[it->second];
    entry.onRemoveComponent(state->registry, entity);
}
} // Game

#endif //WILL_ENGINE_SCENE_SYSTEM_H