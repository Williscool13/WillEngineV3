//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_SYSTEM_H
#define WILL_ENGINE_SCENE_SYSTEM_H
#include <entt/entt.hpp>

#include "game/component-registry/component_registry.h"
#include "game/components/scene_components.h"
#include "core/string_id.h"
#include "core/containers/span.h"
#include "core/containers/arena_fixed_vector.h"
#include "core/containers/arena_vector.h"
#include "core/containers/vector.h"
#include "engine/asset_manager.h"
#include "engine/core/model_id.h"
#include "engine/engine_api.h"
#include "engine/resources/scene/scene.h"
#include "engine/serialization/text_reader.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game
{
Engine::Scene SaveScene(Engine::ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager, StringID sceneId, std::string_view sceneName);

StringID LoadScene(Engine::ComponentRegistry& componentRegistry, entt::registry& registry, const Engine::TextReader& scene);

Core::InlineVector<Engine::Scene, 8> SerializeAll(Engine::ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager,
                                                  Core::Span<Engine::RuntimeSceneMetadata> loadedScenes);

void DeserializeAll(Engine::EngineState* state, Core::Span<Engine::Scene> snapshots);

/**
 * Span must be consistent for this whole function call. Ensure that modification to the following is safe:
 *   state->editor.loadedScenes
 *   state->editor.modifiedScenes
 * @param state
 * @param scenes
 */
void UnloadScenes(Engine::EngineState* state, Core::Span<StringID> scenes);

void UnloadScene(Engine::EngineState* state, StringID sceneId);

void SaveSceneToFile(StringID sceneID, std::string_view sceneName, Engine::EngineState* state, Engine::AssetManager* assetManager, Engine::EngineContext* ctx);

struct LoadSceneResult
{
    bool bSuccess;
    StringID sceneId;
    Core::InlineString<128> sceneName;
};

LoadSceneResult LoadSceneFromFile(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID sceneId);

void SaveEntityAsPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, Engine::EngineContext* ctx, entt::entity entity, std::string_view prefabName);

entt::entity SpawnPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID prefabId, const glm::vec3& spawnPosition = {});

void ResolvePrefabLoads(Engine::EngineState* state, Engine::AssetManager* assetManager);

Core::ArenaVector<entt::entity> SpawnModel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::ModelID modelId, const glm::vec3& offset = {});

/**
 * Splits one flattened primitive out of a whole-model static-mesh entity into a new child entity that renders only that primitive (its own material/transform).
 */
entt::entity SplitOffMeshPrimitive(Engine::EngineState* state, entt::entity parent, uint32_t primitiveOrdinal, const glm::mat4& nodeModelSpace);


/**
 * Resolves every HierarchyComponent's runtime parent handle from its serialized parentStableId. Call after loading entities.
 * @param state
 */
void ResolveHierarchyLinks(Engine::EngineState* state);

/**
 * Refreshes the hierarchy iteration order iff a topology change flagged it dirty; a no-op otherwise.
 * Called by the children-resolve pass; any system that iterates the depth-sorted HierarchyComponent pool itself should call this first.
 */
void EnsureHierarchyOrder(Engine::EngineState* state);

/**
 * Parents child under parent, preserving the child's world pose (rejects cycles).
 */
void SetParent(Engine::EngineState* state, entt::entity child, entt::entity parent);

/**
 * Detaches child from its parent, preserving its world pose.
 */
void ClearParent(Engine::EngineState* state, entt::entity child);

/**
 * Sets an entity's world transform by writing the equivalent local transform (O(1): reads the parent's cached world, one frame behind by design).
 */
void SetWorldTransform(Engine::EngineState* state, entt::entity entity, const Transform& world);

entt::entity CreateSceneEntity(Engine::EngineState* state);

/** Highest sortOrder value among entities in the given scene (0 if none). */
uint64_t HighestSortOrderInScene(entt::registry& registry, StringID sceneId);

/** Base name of sourceName with the highest matching trailing index in the scene incremented by 1. */
Core::InlineString<256> GenerateIncrementedName(entt::registry& registry, StringID sceneId, const Core::InlineString<256>& sourceName);

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
    auto it = state->componentRegistry.registryMapping.Find(TypeSID<T>());
    assert(it != nullptr && "Component type not registered");
    if (it == nullptr) { return false; }
    Engine::ComponentEntry& entry = state->componentRegistry.registry[*it];
    if (!entry.canAdd(state->registry, entity)) { return false; }
    entry.emplaceDefault(state->registry, entity);
    return true;
}

template<typename T>
void DestroyComponent(Engine::EngineState* state, entt::entity entity)
{
    auto it = state->componentRegistry.registryMapping.Find(TypeSID<T>());
    if (it == nullptr) return;
    Engine::ComponentEntry& entry = state->componentRegistry.registry[*it];
    entry.remove(state->registry, entity);
}

inline bool CreateComponent(Engine::EngineState* state, entt::entity entity, StringID typeId)
{
    auto it = state->componentRegistry.registryMapping.Find(typeId);
    assert(it != nullptr && "Component type not registered");
    Engine::ComponentEntry& entry = state->componentRegistry.registry[*it];
    if (!entry.canAdd(state->registry, entity)) { return false; }
    entry.emplaceDefault(state->registry, entity);
    return true;
}

inline void DestroyComponent(Engine::EngineState* state, entt::entity entity, StringID typeId)
{
    auto it = state->componentRegistry.registryMapping.Find(typeId);
    assert(it != nullptr && "Component type not registered");
    Engine::ComponentEntry& entry = state->componentRegistry.registry[*it];
    entry.remove(state->registry, entity);
}

void PlayStart(Engine::EngineContext* ctx, Engine::EngineState* state);

void PlayStop(Engine::EngineContext* ctx, Engine::EngineState* state);

/** True while a PIE session (Gameplay or its paused Menu) is active. Not just "!= Editor" - Console is a third,
 *  orthogonal modal state that can overlay either Editor or a play session, so it must not count as "playing". */
inline bool IsPlaying(const Engine::EngineState* state)
{
    return state->inputContext == Engine::InputContext::Gameplay || state->inputContext == Engine::InputContext::Menu;
}
} // Game

#endif //WILL_ENGINE_SCENE_SYSTEM_H
