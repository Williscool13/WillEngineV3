//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_SYSTEM_H
#define WILL_ENGINE_SCENE_SYSTEM_H
#include <entt/entt.hpp>

#include "../component-registry/component_registry.h"
#include "game/components/scene_components.h"
#include "core/string_id.h"
#include "core/containers/span.h"
#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/core/model_id.h"
#include "engine/engine_api.h"
#include "engine/resources/scene/scene.h"

namespace Core
{
struct EngineContext;
}

namespace Engine
{
struct EngineState;
}

namespace Game
{
Engine::Scene SaveScene(ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager, StringID sceneId, std::string_view sceneName);

StringID LoadScene(ComponentRegistry& componentRegistry, entt::registry& registry, Engine::Scene& scene);

Core::InlineVector<Engine::Scene, 8> SerializeAll(ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager, Core::Span<Engine::EngineState::RuntimeSceneMetadata> loadedScenes);

void DeserializeAll(Engine::EngineState* state, Core::Span<Engine::Scene> snapshots);

void UnloadScene(Engine::EngineState* state, StringID sceneId);

void SaveSceneToFile(StringID sceneID, std::string_view sceneName, Engine::EngineState* state, Engine::AssetManager* assetManager, Core::EngineContext* ctx);

bool LoadSceneFromFile(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID sceneId);

void SaveEntityAsPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, Core::EngineContext* ctx, entt::entity entity, std::string_view prefabName);

entt::entity SpawnPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID prefabId, const glm::vec3& spawnPosition = {});

void ResolvePrefabLoads(Engine::EngineState* state, Engine::AssetManager* assetManager);

std::vector<entt::entity> SpawnModel(Core::EngineContext* ctx, Engine::EngineState* state, Engine::ModelID modelId, const glm::vec3& offset = {});

entt::entity CreateSceneEntity(Engine::EngineState* state);

/**
 * Copies all registered components from src to a new entity.
 * Signals (on_construct) fire during emplace, handling initialization.
 */
inline entt::entity CopyEntity(Engine::EngineState* state, entt::entity src)
{
    entt::entity dst = state->registry.create();

    for (auto& entry : state->componentRegistry.registry) {
        if (entry.has(state->registry, src)) {
            entry.copy(state->registry, src, state->registry, dst);
        }
    }

    return dst;
}

/**
 * Wraps CopyEntity and assigns SceneComponent. Editor use.
 * Pass a targetScene to copy into a different scene, otherwise inherits src's scene.
 * @param state
 * @param src
 * @param targetScene
 * @return
 */
inline entt::entity CopySceneEntity(Engine::EngineState* state, entt::entity src, StringID targetScene = {})
{
    entt::entity dst = CopyEntity(state, src);

    if (const auto* scene = state->registry.try_get<Component::SceneComponent>(src)) {
        const StringID sceneId = targetScene ? targetScene : scene->sceneId;
        state->registry.emplace<Component::SceneComponent>(dst, sceneId);
    }

    return dst;
}

template<typename T>
bool CreateComponent(Engine::EngineState* state, entt::entity entity)
{
    auto it = state->componentRegistry.registryMapping.find(TypeSID<T>());
    assert(it != state->componentRegistry.registryMapping.end() && "Component type not registered");
    ComponentEntry& entry = state->componentRegistry.registry[it->second];
    if (!entry.canAdd(state->registry, entity)) { return false; }
    entry.emplaceDefault(state->registry, entity);
    return true;
}

template<typename T>
void DestroyComponent(Engine::EngineState* state, entt::entity entity)
{
    auto it = state->componentRegistry.registryMapping.find(TypeSID<T>());
    if (it == state->componentRegistry.registryMapping.end()) return;
    ComponentEntry& entry = state->componentRegistry.registry[it->second];
    entry.remove(state->registry, entity);
}

inline bool CreateComponent(Engine::EngineState* state, entt::entity entity, StringID typeId)
{
    auto it = state->componentRegistry.registryMapping.find(typeId);
    assert(it != state->componentRegistry.registryMapping.end() && "Component type not registered");
    ComponentEntry& entry = state->componentRegistry.registry[it->second];
    if (!entry.canAdd(state->registry, entity)) { return false; }
    entry.emplaceDefault(state->registry, entity);
    return true;
}

inline void DestroyComponent(Engine::EngineState* state, entt::entity entity, StringID typeId)
{
    auto it = state->componentRegistry.registryMapping.find(typeId);
    assert(it != state->componentRegistry.registryMapping.end() && "Component type not registered");
    ComponentEntry& entry = state->componentRegistry.registry[it->second];
    entry.remove(state->registry, entity);
}

void PlayStart(Core::EngineContext* ctx, Engine::EngineState* state);

void PlayStop(Core::EngineContext* ctx, Engine::EngineState* state);
} // Game

#endif //WILL_ENGINE_SCENE_SYSTEM_H
