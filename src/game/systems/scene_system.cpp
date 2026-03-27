//
// Created by William on 2026-02-26.
//

#include "scene_system.h"

#include <algorithm>
#include <fstream>
#include <limits>

#include <json/nlohmann/json.hpp>

#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/scene/scene.h"
#include "engine/resources/prefab/prefab_format.h"
#include "engine/resources/scene/scene_format.h"
#include "game/components/common_components.h"
#include "game/components/common/stable_id_component.h"
#include "game/components/core_components.h"
#include "game/components/editor_components.h"
#include "game/components/gameplay/player_spawn_component.h"
#include "game/components/render_components.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/scene_components.h"
#include "game/gameplay/player/physics_player_controller.h"
#include "game/systems/physics_system.h"
#include "platform/paths.h"

namespace Game
{
Engine::Scene SaveScene(ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager, StringID sceneId, std::string_view sceneName)
{
    Engine::Scene outScene{};
    nlohmann::json& scene = outScene.content;

    scene["scene_id"] = sceneId.id;
    scene["scene_name"] = sceneName;
    scene["entities"] = nlohmann::json::array();

    auto view = registry.view<Component::SceneComponent>();
    const StringID prefabTypeId = TypeSID<Component::PrefabInstanceComponent>();

    // Cache prefab JSON per-prefab so we only read each file once
    std::unordered_map<StringID, nlohmann::json> prefabJsonCache;

    for (auto entity : view) {
        auto& tag = view.get<Component::SceneComponent>(entity);
        if (tag.sceneId != sceneId) {
            continue;
        }
        if (registry.all_of<Component::DoNotSerializeTag>(entity)) {
            continue;
        }

        nlohmann::json entityJson;
        const auto* prefabInst = registry.try_get<Component::PrefabInstanceComponent>(entity);

        const nlohmann::json* prefabRef = nullptr;
        if (prefabInst) {
            auto cacheIt = prefabJsonCache.find(prefabInst->prefabId);
            if (cacheIt == prefabJsonCache.end()) {
                if (const auto* meta = assetManager->GetPrefabMetadata(prefabInst->prefabId)) {
                    auto prefabData = Engine::ReadWPrefab(meta->source);
                    if (prefabData) {
                        cacheIt = prefabJsonCache.emplace(prefabInst->prefabId, std::move(prefabData->componentJson)).first;
                    }
                }
            }
            if (cacheIt != prefabJsonCache.end()) {
                prefabRef = &cacheIt->second;
            }
        }

        for (ComponentEntry& entry : componentRegistry.registry) {
            if (!entry.has(registry, entity)) {
                continue;
            }

            nlohmann::json compJson;
            entry.serialize(registry, entity, compJson);

            if (prefabRef) {
                const std::string key = std::to_string(entry.typeId.id);
                if (entry.typeId == prefabTypeId) {
                    entityJson[key] = compJson;
                }
                else {
                    auto it = prefabRef->find(key);
                    if (it == prefabRef->end() || *it != compJson) {
                        entityJson[key] = compJson;
                    }
                }
            }
            else {
                entityJson[std::to_string(entry.typeId.id)] = compJson;
            }
        }

        scene["entities"].push_back(entityJson);
    }

    return outScene;
}

StringID LoadScene(ComponentRegistry& componentRegistry, entt::registry& registry, Engine::Scene& scene)
{
    auto sceneId = StringID(scene.content["scene_id"].get<uint64_t>());

    std::vector<entt::entity> sceneEntities;
    sceneEntities.reserve(scene.content["entities"].size());

    for (auto& entityJson : scene.content["entities"]) {
        auto entity = registry.create();
        for (auto& [key, compJson] : entityJson.items()) {
            uint64_t typeId = std::stoull(key);
            for (ComponentEntry& entry : componentRegistry.registry) {
                if (entry.typeId.id == typeId) {
                    entry.deserialize(registry, entity, compJson);
                    break;
                }
            }
        }
        registry.emplace<Component::SceneComponent>(entity, sceneId);
        sceneEntities.push_back(entity);
    }

    return sceneId;
}

std::vector<Engine::Scene> SerializeAll(ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager, const std::vector<StringID>& loadedScenes)
{
    std::vector<Engine::Scene> snapshots;
    snapshots.reserve(loadedScenes.size());
    for (StringID sceneId : loadedScenes) {
        snapshots.push_back(SaveScene(componentRegistry, registry, assetManager, sceneId, sceneId.ToString()));
        LOG_INFO(Game, "PIE snapshot: serialized scene '{}'", sceneId.ToString());
    }
    return snapshots;
}

void DeserializeAll(Engine::GameState* state, std::vector<Engine::Scene>& snapshots)
{
    std::vector<StringID> toUnload = state->loadedScenes;
    for (StringID sceneId : toUnload) {
        UnloadScene(state, sceneId);
        LOG_INFO(Game, "PIE restore: unloaded scene '{}'", sceneId.ToString());
    }

    for (Engine::Scene& scene : snapshots) {
        StringID loadedId = LoadScene(state->componentRegistry, state->registry, scene);
        state->loadedScenes.push_back(loadedId);
        LOG_INFO(Game, "PIE restore: reloaded scene '{}'", loadedId.ToString());
    }

    auto* ctx = state->registry.ctx().get<Core::EngineContext*>();
    ResolvePrefabLoads(state, ctx->assetManager);
}

void UnloadScene(Engine::GameState* state, StringID sceneId)
{
    auto view = state->registry.view<Component::SceneComponent>();
    std::vector<entt::entity> toDestroy;
    for (auto entity : view) {
        if (view.get<Component::SceneComponent>(entity).sceneId == sceneId) {
            toDestroy.push_back(entity);
        }
    }

    for (entt::entity entity : toDestroy) {
        state->registry.destroy(entity);
    }

    std::erase_if(state->selectedEntities, [&](entt::entity e) {
        return std::ranges::find(toDestroy, e) != toDestroy.end();
    });

    std::erase(state->loadedScenes, sceneId);
    std::erase(state->modifiedScenes, sceneId);
}

void SaveSceneToFile(StringID sceneID, std::string_view sceneName, Engine::GameState* state, Engine::AssetManager* assetManager, Core::EngineContext* ctx)
{
    const auto& sceneCache = assetManager->GetSceneCache();
    std::filesystem::path path;

    auto it = sceneCache.find(sceneID);
    if (it != sceneCache.end() && !it->second.source.empty()) {
        path = it->second.source;
    }
    else {
        std::string stem(sceneName);
        std::ranges::transform(stem, stem.begin(), ::tolower);
        std::ranges::replace(stem, ' ', '_');
        path = Platform::GetAssetPath() / "scenes" / (stem + ".wscene");
    }

    Engine::Scene s = SaveScene(state->componentRegistry, state->registry, assetManager, sceneID, sceneName);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);

    Engine::WSceneHeader sceneHeader{};
    sceneHeader.sceneId = sceneID.id;
    const auto nameLen = std::min(sceneName.size(), Engine::WSCENE_NAME_LENGTH - 1);
    memcpy(sceneHeader.name, sceneName.data(), nameLen);
    sceneHeader.name[nameLen] = '\0';
    sceneHeader.entityCount = static_cast<uint32_t>(s.content["entities"].size());
    Engine::WriteWSceneHeader(file, sceneHeader);

    file << s.content.dump(2);

    assetManager->UpdateSceneCachePath(sceneID, path, sceneHeader.entityCount);

    LOG_INFO(Game, "Saved scene '{}' to '{}'", sceneName, path.string());
}

bool LoadSceneFromFile(Engine::GameState* state, Engine::AssetManager* assetManager, StringID sceneId)
{
    if (std::ranges::find(state->loadedScenes, sceneId) != state->loadedScenes.end()) {
        LOG_WARN(Game, "Scene '{}' is already loaded", sceneId.ToString());
        return false;
    }

    const auto& sceneCache = assetManager->GetSceneCache();
    auto it = sceneCache.find(sceneId);
    if (it == sceneCache.end()) {
        LOG_ERROR(Game, "Scene ID not found in registry");
        return false;
    }

    const std::filesystem::path& path = it->second.source;
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR(Game, "Failed to open scene file '{}'", path.string());
        return false;
    }

    auto header = Engine::ReadWSceneHeader(file);
    if (!header) {
        LOG_ERROR(Game, "Failed to read scene header from '{}'", path.string());
        return false;
    }

    Engine::Scene s;
    s.content = nlohmann::json::parse(file);
    StringID loadedId = LoadScene(state->componentRegistry, state->registry, s);
    assert(loadedId == sceneId && "Scene ID in file does not match registry key, file was likely saved with a mismatched ID");
    state->loadedScenes.push_back(loadedId);
    std::erase(state->modifiedScenes, loadedId);

    ResolvePrefabLoads(state, assetManager);

    LOG_INFO(Game, "Loaded scene '{}' from '{}'", sceneId.ToString(), path.string());
    return true;
}

std::vector<entt::entity> SpawnModel(Engine::GameState* state, Engine::AssetManager* assetManager, Engine::ModelID modelId, const glm::vec3& offset)
{
    const Engine::AssetManager::CachedModelMetadata* cached = assetManager->GetModelMetadata(modelId);
    if (!cached) {
        LOG_ERROR(Game, "SpawnModel: modelId {} not in asset registry", modelId.id);
        return {};
    }

    const auto& nodes = cached->nodes;

    std::vector<glm::vec3> worldT(nodes.size());
    std::vector<glm::quat> worldR(nodes.size());
    std::vector<glm::vec3> worldS(nodes.size());

    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (node.parent == ~0u) {
            worldT[i] = node.localTranslation;
            worldR[i] = node.localRotation;
            worldS[i] = node.localScale;
        }
        else {
            worldT[i] = worldR[node.parent] * (worldS[node.parent] * node.localTranslation) + worldT[node.parent];
            worldR[i] = worldR[node.parent] * node.localRotation;
            worldS[i] = worldS[node.parent] * node.localScale;
        }
    }

    std::vector<entt::entity> spawned;

    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (node.meshIndex == ~0u) continue;

        entt::entity entity = CreateSceneEntity(state);

        if (!node.name.empty()) {
            state->registry.get<Component::NameComponent>(entity).name = StackString<256>(node.name.c_str());
        }

        auto& transform = state->registry.get<Component::TransformComponent>(entity);
        transform.translation = worldT[i] + offset;
        transform.rotation = worldR[i];
        transform.scale = worldS[i];

        Component::StaticMeshComponent meshComp{};
        meshComp.modelId = modelId;
        meshComp.meshIndex = static_cast<int32_t>(node.meshIndex);
        meshComp.modelFlags = {1.0f, 1.0f, 0.0f, 0.0f};
        state->registry.emplace<Component::StaticMeshComponent>(entity, std::move(meshComp));

        spawned.push_back(entity);
    }

    return spawned;
}

entt::entity CreateSceneEntity(Engine::GameState* state)
{
    entt::entity newEntity = state->registry.create();
    state->registry.emplace<Component::TransformComponent>(newEntity);
    state->registry.emplace<Component::SceneComponent>(newEntity, state->currentSceneId);
    state->registry.emplace<Component::StableIdComponent>(newEntity);
    state->registry.emplace<Component::EntityFolderComponent>(newEntity);
    static int32_t runningNameTally = 0;
    auto newName = fmt::format("New Entity {}", runningNameTally++);
    state->registry.emplace<Component::NameComponent>(newEntity, StackString<256>(newName.c_str()));
    LOG_TRACE(Game, "Created new entity {}", entt::to_integral(newEntity));
    return newEntity;
}

void SaveEntityAsPrefab(Engine::GameState* state, Engine::AssetManager* assetManager, Core::EngineContext* ctx, entt::entity entity, std::string_view prefabName)
{
    // todo: Fix this to:
    //  Compare all existing prefabs and check their components. If their component fields are precisely the same as the src prefab, then replace it with new
    nlohmann::json entityJson;
    uint32_t componentCount = 0;

    for (ComponentEntry& entry : state->componentRegistry.registry) {
        if (entry.has(state->registry, entity)) {
            nlohmann::json compJson;
            entry.serialize(state->registry, entity, compJson);
            entityJson[std::to_string(entry.typeId.id)] = compJson;
            componentCount++;
        }
    }

    std::filesystem::path path;
    uint64_t prefabId = 0;
    bool isNewPrefab = true;

    auto* prefabInstance = state->registry.try_get<Component::PrefabInstanceComponent>(entity);
    if (prefabInstance) {
        const auto* meta = assetManager->GetPrefabMetadata(StringID{prefabInstance->prefabId});
        if (meta) {
            path = meta->source;
            prefabId = prefabInstance->prefabId.id;
            isNewPrefab = false;
        }
    }

    if (isNewPrefab) {
        std::string stem(prefabName);
        std::ranges::transform(stem, stem.begin(), ::tolower);
        std::ranges::replace(stem, ' ', '_');
        path = Platform::GetAssetPath() / "prefabs" / (stem + ".wprefab");
        prefabId = state->rng();
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);

    Engine::WPrefabHeader header{};
    header.prefabId = prefabId;
    const auto nameLen = std::min(prefabName.size(), Engine::WPREFAB_NAME_LENGTH - 1);
    memcpy(header.name, prefabName.data(), nameLen);
    header.name[nameLen] = '\0';
    header.componentCount = componentCount;
    Engine::WriteWPrefabHeader(file, header);

    file << entityJson.dump(2);

    state->registry.emplace_or_replace<Component::PrefabInstanceComponent>(entity, StringID{prefabId});

    if (isNewPrefab) {
        ctx->bShouldRescanResources.store(true, std::memory_order_release);
    }
    LOG_INFO(Game, "Saved prefab '{}' to '{}'", prefabName, path.string());
}

entt::entity SpawnPrefab(Engine::GameState* state, Engine::AssetManager* assetManager, StringID prefabId, const glm::vec3& spawnPosition)
{
    const auto* meta = assetManager->GetPrefabMetadata(prefabId);
    if (!meta) {
        LOG_ERROR(Game, "Prefab '{}' not found in registry", prefabId.ToString());
        return entt::null;
    }

    auto prefabData = Engine::ReadWPrefab(meta->source);
    if (!prefabData) {
        LOG_ERROR(Game, "Failed to read prefab file '{}'", meta->source.string());
        return entt::null;
    }

    entt::entity entity = state->registry.create();
    for (auto& [key, compJson] : prefabData->componentJson.items()) {
        uint64_t typeId = std::stoull(key);
        for (ComponentEntry& entry : state->componentRegistry.registry) {
            if (entry.typeId.id == typeId) {
                entry.deserialize(state->registry, entity, compJson);
                break;
            }
        }
    }

    if (auto* transform = state->registry.try_get<Component::TransformComponent>(entity)) {
        transform->translation = spawnPosition;
    }

    state->registry.emplace<Component::SceneComponent>(entity, state->currentSceneId);
    state->registry.emplace_or_replace<Component::PrefabInstanceComponent>(entity, prefabId);

    LOG_INFO(Game, "Spawned prefab '{}' as entity {}", meta->prefabName, entt::to_integral(entity));
    return entity;
}

void ResolvePrefabLoads(Engine::GameState* state, Engine::AssetManager* assetManager)
{
    // Cache parsed prefab JSON so each file is read once even with many instances
    std::unordered_map<StringID, nlohmann::json> prefabJsonCache;

    auto view = state->registry.view<Component::PrefabInstanceComponent>();
    for (auto entity : view) {
        auto& prefabInst = view.get<Component::PrefabInstanceComponent>(entity);

        const auto* meta = assetManager->GetPrefabMetadata(prefabInst.prefabId);
        if (!meta) {
            LOG_WARN(Game, "Prefab '{}' not found for entity {}, skipping", prefabInst.prefabId.ToString(), entt::to_integral(entity));
            continue;
        }

        auto cacheIt = prefabJsonCache.find(prefabInst.prefabId);
        if (cacheIt == prefabJsonCache.end()) {
            auto prefabData = Engine::ReadWPrefab(meta->source);
            if (!prefabData) {
                LOG_WARN(Game, "Failed to read prefab file '{}'", meta->source.string());
                continue;
            }
            cacheIt = prefabJsonCache.emplace(prefabInst.prefabId, std::move(prefabData->componentJson)).first;
        }

        for (auto& [key, compJson] : cacheIt->second.items()) {
            uint64_t typeId = std::stoull(key);
            for (ComponentEntry& entry : state->componentRegistry.registry) {
                if (entry.typeId.id == typeId) {
                    if (!entry.has(state->registry, entity)) {
                        entry.deserialize(state->registry, entity, compJson);
                    }
                    break;
                }
            }
        }
    }
}

void PlayStart(Core::EngineContext* ctx, Engine::GameState* state)
{
    state->pieSnapshot = SerializeAll(state->componentRegistry, state->registry, ctx->assetManager, state->loadedScenes);

    {
        std::vector<entt::entity> masterPrefabs;
        auto view = state->registry.view<Component::PrefabInstanceComponent>();
        for (auto entity : view) {
            if (view.get<Component::PrefabInstanceComponent>(entity).bMasterPrefab) {
                masterPrefabs.push_back(entity);
            }
        }
        for (auto entity : masterPrefabs) {
            state->registry.destroy(entity);
        }
    }

    state->bIsPlaying = true;
    state->bGameCursorCaptured = true;
    ctx->setCursorHiddenFn(true);

    glm::vec3 spawnPosition{0.0f, 3.0f, 0.0f};
    {
        int32_t bestPriority = std::numeric_limits<int32_t>::min();
        auto spawnView = state->registry.view<Component::PlayerSpawnComponent, Component::TransformComponent>();
        for (auto entity : spawnView) {
            auto& spawn = spawnView.get<Component::PlayerSpawnComponent>(entity);
            if (spawn.priority > bestPriority) {
                bestPriority = spawn.priority;
                spawnPosition = spawnView.get<Component::TransformComponent>(entity).translation + spawn.offset;
            }
        }
    }

    auto& playerController = state->registry.ctx().emplace<PhysicsPlayerController>();
    playerController.Initialize(state, ctx, spawnPosition);
}

void PlayStop(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (auto* playerController = state->registry.ctx().find<PhysicsPlayerController>()) {
        playerController->Shutdown(ctx->physicsSystem);
        state->registry.ctx().erase<PhysicsPlayerController>();
    }

    PhysicsOnPlayStop(ctx, state);

    DeserializeAll(state, state->pieSnapshot);
    state->pieSnapshot.clear();

    state->bIsPlaying = false;
    state->bGameCursorCaptured = false;
    ctx->setCursorHiddenFn(false);
}
} // Game
