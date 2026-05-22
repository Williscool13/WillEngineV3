//
// Created by William on 2026-02-26.
//

#include "scene_system.h"

#include <algorithm>
#include <fstream>
#include <limits>

#include <json/nlohmann/json.hpp>

#include "core/containers/arena_array.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/scene/scene.h"
#include "engine/resources/prefab/prefab_format.h"
#include "engine/resources/scene/scene_format.h"
#include "game/components/camera_components.h"
#include "game/components/common_components.h"
#include "game/components/common/stable_id_component.h"
#include "game/components/core_components.h"
#include "game/components/editor_components.h"
#include "game/components/gameplay/player_spawn_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/scene_components.h"
#include "game/gameplay/player/physics_player_controller.h"
#include "game/systems/physics_system.h"
#include "platform/file_utils.h"
#include "platform/paths.h"

namespace Game
{
Engine::Scene SaveScene(Engine::ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager, StringID sceneId, std::string_view sceneName)
{
    Engine::Scene outScene{};
    nlohmann::json& scene = outScene.content;

    scene["scene_id"] = sceneId.id;
    scene["scene_name"] = sceneName;
    scene["entities"] = nlohmann::json::array();

    auto view = registry.view<Component::SceneComponent>();
    const StringID prefabTypeId = TypeSID<Component::PrefabInstanceComponent>();

    // Cache prefab JSON per-prefab so we only read each file once
    Core::InlineMap<StringID, nlohmann::json, 512> prefabJsonCache;

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
            auto cacheIt = prefabJsonCache.Find(prefabInst->prefabId);
            if (cacheIt) {
                if (const auto* meta = assetManager->GetPrefabMetadata(prefabInst->prefabId)) {
                    auto prefabData = Engine::ReadWPrefab(meta->source.c_str());
                    if (prefabData) {
                        cacheIt = &prefabJsonCache.Emplace(prefabInst->prefabId, std::move(prefabData->componentJson));
                    }
                }
            }
            if (cacheIt) {
                prefabRef = cacheIt;
            }
        }

        for (Engine::ComponentEntry& entry : componentRegistry.registry) {
            if (!entry.has(registry, entity)) {
                continue;
            }

            nlohmann::json compJson;
            entry.serialize(registry, entity, compJson);

            auto s = Core::ShortString::Format("%llu", entry.typeId.id);
            if (prefabRef) {
                if (entry.typeId == prefabTypeId) {
                    entityJson[s.c_str()] = compJson;
                }
                else {
                    auto it = prefabRef->find(s.c_str());
                    if (it == prefabRef->end() || *it != compJson) {
                        entityJson[s.c_str()] = compJson;
                    }
                }
            }
            else {
                entityJson[s.c_str()] = compJson;
            }
        }

        scene["entities"].push_back(entityJson);
    }

    return outScene;
}

StringID LoadScene(Engine::ComponentRegistry& componentRegistry, entt::registry& registry, Engine::Scene& scene)
{
    auto sceneId = StringID(scene.content["scene_id"].get<uint64_t>());

    for (auto& entityJson : scene.content["entities"]) {
        auto entity = registry.create();
        for (auto& [key, compJson] : entityJson.items()) {
            uint64_t typeId = std::stoull(key);
            for (Engine::ComponentEntry& entry : componentRegistry.registry) {
                if (entry.typeId.id == typeId) {
                    entry.deserialize(registry, entity, compJson);
                    break;
                }
            }
        }
        registry.emplace<Component::SceneComponent>(entity, sceneId);
    }

    return sceneId;
}

Core::InlineVector<Engine::Scene, 8> SerializeAll(Engine::ComponentRegistry& componentRegistry, entt::registry& registry, Engine::AssetManager* assetManager, Core::Span<Engine::RuntimeSceneMetadata> loadedScenes)
{
    Core::InlineVector<Engine::Scene, 8> snapshots;
    for (int i = 0; i < loadedScenes.Size(); ++i) {
        auto& meta = loadedScenes[i];
        snapshots.PushBack(SaveScene(componentRegistry, registry, assetManager, meta.sceneId, meta.sceneId.ToString()));
        LOG_INFO(Game, "PIE snapshot: serialized scene '{}'", meta.sceneId.ToString());
    }
    return snapshots;
}

void DeserializeAll(Engine::EngineState* state, Core::Span<Engine::Scene> snapshots)
{
    Core::InlineVector<Engine::RuntimeSceneMetadata, 8> toUnload = state->editor.loadedScenes;
    for (const auto& meta : toUnload) {
        UnloadScene(state, meta.sceneId);
        LOG_INFO(Game, "PIE restore: unloaded scene '{}'", meta.sceneId.ToString());
    }

    for (Engine::Scene& scene : snapshots) {
        StringID loadedId = LoadScene(state->componentRegistry, state->registry, scene);
        uint64_t maxSortOrder = 0;
        auto sortView = state->registry.view<Component::SceneComponent, Component::StableIdComponent>();
        for (auto entity : sortView) {
            if (sortView.get<Component::SceneComponent>(entity).sceneId == loadedId) {
                maxSortOrder = std::max(maxSortOrder, sortView.get<Component::StableIdComponent>(entity).sortOrder);
            }
        }
        for (auto entity : sortView) {
            if (sortView.get<Component::SceneComponent>(entity).sceneId == loadedId) {
                auto& stable = sortView.get<Component::StableIdComponent>(entity);
                if (stable.sortOrder == 0) {
                    maxSortOrder += 100;
                    stable.sortOrder = maxSortOrder;
                }
            }
        }
        state->editor.loadedScenes.PushBack({loadedId, maxSortOrder + 100});
        LOG_INFO(Game, "PIE restore: reloaded scene '{}'", loadedId.ToString());
    }

    auto* ctx = state->registry.ctx().get<Engine::EngineContext*>();
    ResolvePrefabLoads(state, ctx->assetManager);
}

void UnloadScene(Engine::EngineState* state, StringID sceneId)
{
    auto* ctx = state->registry.ctx().get<Engine::EngineContext*>();
    auto view = state->registry.view<Component::SceneComponent>();

    auto size = view.size();
    Core::ArenaVector<entt::entity> toDestroy{&ctx->gameplayArena.Get(), size};
    for (auto entity : view) {
        if (view.get<Component::SceneComponent>(entity).sceneId == sceneId) {
            toDestroy.PushBack(entity);
        }
    }

    for (entt::entity entity : toDestroy) {
        state->registry.destroy(entity);
    }

    state->editor.selectedEntities.Clear();
    state->editor.loadedScenes.RemoveFirstIf([sceneId](const Engine::RuntimeSceneMetadata& m) {
        return m.sceneId == sceneId;
    });
    state->editor.modifiedScenes.RemoveFirst(sceneId);
}

void SaveSceneToFile(StringID sceneID, std::string_view sceneName, Engine::EngineState* state, Engine::AssetManager* assetManager, Engine::EngineContext* ctx)
{
    const auto& sceneCache = assetManager->GetSceneCache();
    Core::Path path;

    auto it = sceneCache.Find(sceneID);
    if (it && !it->source.IsEmpty()) {
        path = it->source;
    }
    else {
        auto stem = Core::InlineString<128>(sceneName);
        std::ranges::transform(stem.buf, stem.buf + stem.len, stem.buf, tolower);
        std::ranges::replace(stem.buf, stem.buf + stem.len, ' ', '_');
        stem.Append(".wscene");
        path = Platform::GetAssetPath() / "scenes" / stem.c_str();
        assert(path.Extension() == ".wscene");
    }

    Engine::Scene s = SaveScene(state->componentRegistry, state->registry, assetManager, sceneID, sceneName);

    {
        auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = state->registry.get<Component::TransformComponent>(camEntity);
            nlohmann::json& camJson = s.content["editor_camera"];
            camJson["translation"] = {transform.translation.x, transform.translation.y, transform.translation.z};
            camJson["rotation"] = {transform.rotation.w, transform.rotation.x, transform.rotation.y, transform.rotation.z};
        }
    }

    Platform::CreateDirectories(path.Parent().c_str());
    std::ofstream file(path.c_str());

    Engine::WSceneHeader sceneHeader{};
    sceneHeader.sceneId = sceneID.id;
    const auto nameLen = std::min(sceneName.size(), Engine::WSCENE_NAME_LENGTH - 1);
    memcpy(sceneHeader.name, sceneName.data(), nameLen);
    sceneHeader.name[nameLen] = '\0';
    sceneHeader.entityCount = static_cast<uint32_t>(s.content["entities"].size());
    Engine::WriteWSceneHeader(file, sceneHeader);

    file << s.content.dump(2);

    assetManager->UpdateSceneCachePath(sceneID, path, sceneHeader.entityCount);

    LOG_INFO(Game, "Saved scene '{}' to '{}'", sceneName, path.c_str());
}

LoadSceneResult LoadSceneFromFile(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID sceneId)
{
    if (std::ranges::any_of(state->editor.loadedScenes, [&](const auto& m) { return m.sceneId == sceneId; })) {
        LOG_WARN(Game, "Scene '{}' is already loaded", sceneId.ToString());
        return {false, sceneId, {}};
    }

    const auto& sceneCache = assetManager->GetSceneCache();
    const auto* it = sceneCache.Find(sceneId);
    if (!it) {
        LOG_ERROR(Game, "Scene ID not found in registry");
        return {false, sceneId, {}};
    }

    const Core::InlineString<128> sceneName = it->sceneName;
    const Core::Path& path = it->source;
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        LOG_ERROR(Game, "Failed to open scene file '{}'", path.c_str());
        return {false, sceneId, sceneName};
    }

    auto header = Engine::ReadWSceneHeader(file);
    if (!header) {
        LOG_ERROR(Game, "Failed to read scene header from '{}'", path.c_str());
        return {false, sceneId, sceneName};
    }

    Engine::Scene s;
    s.content = nlohmann::json::parse(file);
    StringID loadedId = LoadScene(state->componentRegistry, state->registry, s);
    assert(loadedId == sceneId && "Scene ID in file does not match registry key, file was likely saved with a mismatched ID");
    uint64_t maxSortOrder = 0;
    {
        auto sortView = state->registry.view<Component::SceneComponent, Component::StableIdComponent>();
        for (auto entity : sortView) {
            if (sortView.get<Component::SceneComponent>(entity).sceneId == loadedId) {
                maxSortOrder = std::max(maxSortOrder, sortView.get<Component::StableIdComponent>(entity).sortOrder);
            }
        }
        for (auto entity : sortView) {
            if (sortView.get<Component::SceneComponent>(entity).sceneId == loadedId) {
                auto& stable = sortView.get<Component::StableIdComponent>(entity);
                if (stable.sortOrder == 0) {
                    maxSortOrder += 100;
                    stable.sortOrder = maxSortOrder;
                }
            }
        }
    }
    state->editor.loadedScenes.PushBack({loadedId, maxSortOrder + 100});
    state->editor.modifiedScenes.RemoveFirst(loadedId);

    ResolvePrefabLoads(state, assetManager);

    if (s.content.contains("editor_camera")) {
        auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& camJson = s.content["editor_camera"];
            auto& transform = state->registry.get<Component::TransformComponent>(camEntity);
            const auto& t = camJson["translation"];
            transform.translation = glm::vec3(t[0].get<float>(), t[1].get<float>(), t[2].get<float>());
            const auto& r = camJson["rotation"];
            transform.rotation = glm::quat(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());
        }
    }

    LOG_INFO(Game, "Loaded scene '{}' from '{}'", sceneId.ToString(), path.c_str());
    return {true, sceneId, sceneName};
}

Core::ArenaVector<entt::entity> SpawnModel(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::ModelID modelId, const glm::vec3& offset)
{
    const Engine::AssetManager::CachedModelMetadata* cached = ctx->assetManager->GetModelMetadata(modelId);
    if (!cached) {
        LOG_ERROR(Game, "SpawnModel: modelId {} not in asset registry", modelId.id);
        return {};
    }

    const auto& nodes = cached->nodes;

     auto worldT = Core::ArenaArray<Vec3>(&ctx->gameplayArena.Get(),nodes.Size());
     auto worldR = Core::ArenaArray<Quat>(&ctx->gameplayArena.Get(),nodes.Size());
     auto worldS = Core::ArenaArray<Vec3>(&ctx->gameplayArena.Get(),nodes.Size());

    for (size_t i = 0; i < nodes.Size(); ++i) {
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

    auto spawned = Core::ArenaVector<entt::entity>(&ctx->gameplayArena.Get(), 32);

    for (size_t i = 0; i < nodes.Size(); ++i) {
        const auto& node = nodes[i];
        if (node.meshIndex == ~0u) continue;

        entt::entity entity = CreateSceneEntity(state);

        if (node.name.Size() > 0) {
            state->registry.get<Component::NameComponent>(entity).name = Core::InlineString<256>(node.name.c_str());
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

        spawned.PushBack(entity);
    }

    return spawned;
}

entt::entity CreateSceneEntity(Engine::EngineState* state)
{
    entt::entity newEntity = state->registry.create();
    state->registry.emplace<Component::TransformComponent>(newEntity);
    state->registry.emplace<Component::SceneComponent>(newEntity, state->currentSceneId);
    state->registry.emplace<Component::StableIdComponent>(newEntity);
    auto metaIt = std::ranges::find_if(state->editor.loadedScenes, [&](const auto& m) { return m.sceneId == state->currentSceneId; });
    if (metaIt != state->editor.loadedScenes.end()) {
        state->registry.get<Component::StableIdComponent>(newEntity).sortOrder = metaIt->nextSortOrder;
        metaIt->nextSortOrder += 100;
    }
    state->registry.emplace<Component::EntityFolderComponent>(newEntity);
    static int32_t runningNameTally = 0;
    auto newName = fmt::format("New Entity {}", runningNameTally++);
    state->registry.emplace<Component::NameComponent>(newEntity, Core::InlineString<256>(newName.c_str()));
    LOG_TRACE(Game, "Created new entity {}", entt::to_integral(newEntity));
    return newEntity;
}

void SaveEntityAsPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, Engine::EngineContext* ctx, entt::entity entity, std::string_view prefabName)
{
    // todo: Fix this to:
    //  Compare all existing prefabs and check their components. If their component fields are precisely the same as the src prefab, then replace it with new
    nlohmann::json entityJson;
    uint32_t componentCount = 0;

    for (Engine::ComponentEntry& entry : state->componentRegistry.registry) {
        if (entry.has(state->registry, entity)) {
            nlohmann::json compJson;
            entry.serialize(state->registry, entity, compJson);
            auto s = Core::ShortString::Format("%llu", entry.typeId.id);
            entityJson[s.c_str()] = compJson;
            componentCount++;
        }
    }

    Core::Path path;
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
        Core::InlineString s{prefabName};
        s.ToLower();
        s.Replace(' ', '_');
        s.Append(".wprefab");
        path = Platform::GetAssetPath() / "prefabs" / s.c_str();
        prefabId = state->rng();
    }

    Platform::CreateDirectories(path.Parent().c_str());
    std::ofstream file(path.c_str());

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
        ctx->bShouldRescanResources = true;
    }
    LOG_INFO(Game, "Saved prefab '{}' to '{}'", prefabName, path.c_str());
}

entt::entity SpawnPrefab(Engine::EngineState* state, Engine::AssetManager* assetManager, StringID prefabId, const glm::vec3& spawnPosition)
{
    const auto* meta = assetManager->GetPrefabMetadata(prefabId);
    if (!meta) {
        LOG_ERROR(Game, "Prefab '{}' not found in registry", prefabId.ToString());
        return entt::null;
    }

    auto prefabData = Engine::ReadWPrefab(meta->source.c_str());
    if (!prefabData) {
        LOG_ERROR(Game, "Failed to read prefab file '{}'", meta->source.c_str());
        return entt::null;
    }

    entt::entity entity = state->registry.create();
    for (auto& [key, compJson] : prefabData->componentJson.items()) {
        uint64_t typeId = std::stoull(key);
        for (Engine::ComponentEntry& entry : state->componentRegistry.registry) {
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

    if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
        if (stable->sortOrder == 0) {
            auto metaIt = std::ranges::find_if(state->editor.loadedScenes, [&](const auto& m) { return m.sceneId == state->currentSceneId; });
            if (metaIt != state->editor.loadedScenes.end()) {
                stable->sortOrder = metaIt->nextSortOrder;
                metaIt->nextSortOrder += 100;
            }
        }
    }

    LOG_INFO(Game, "Spawned prefab '{}' as entity {}", meta->prefabName.c_str(), entt::to_integral(entity));
    return entity;
}

void ResolvePrefabLoads(Engine::EngineState* state, Engine::AssetManager* assetManager)
{
    // Cache parsed prefab JSON so each file is read once even with many instances
    Core::InlineMap<StringID, nlohmann::json, 512> prefabJsonCache;

    auto view = state->registry.view<Component::PrefabInstanceComponent>();
    for (auto entity : view) {
        auto& prefabInst = view.get<Component::PrefabInstanceComponent>(entity);

        const auto* meta = assetManager->GetPrefabMetadata(prefabInst.prefabId);
        if (!meta) {
            LOG_WARN(Game, "Prefab '{}' not found for entity {}, skipping", prefabInst.prefabId.ToString(), entt::to_integral(entity));
            continue;
        }

        auto cacheIt = prefabJsonCache.Find(prefabInst.prefabId);
        if (cacheIt == nullptr) {
            auto prefabData = Engine::ReadWPrefab(meta->source.c_str());
            if (!prefabData) {
                LOG_WARN(Game, "Failed to read prefab file '{}'", meta->source.c_str());
                continue;
            }
            cacheIt = &prefabJsonCache.Emplace(prefabInst.prefabId, std::move(prefabData->componentJson));
        }

        for (auto& [key, compJson] : cacheIt->items()) {
            uint64_t typeId = std::stoull(key);
            for (Engine::ComponentEntry& entry : state->componentRegistry.registry) {
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

void PlayStart(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    {
        auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = state->registry.get<Component::TransformComponent>(camEntity);
            state->editor.pieCameraTranslation = transform.translation;
            state->editor.pieCameraRotation = transform.rotation;
        }
    }

    state->editor.pieSnapshot = SerializeAll(state->componentRegistry, state->registry, ctx->assetManager, state->editor.loadedScenes);

    {
        auto view = state->registry.view<Component::PrefabInstanceComponent>();
        if (view.size() > 0) {
            auto masterPrefabs = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size());
            for (auto entity : view) {
                if (view.get<Component::PrefabInstanceComponent>(entity).bMasterPrefab) {
                    masterPrefabs.PushBack(entity);
                }
            }
            for (auto entity : masterPrefabs) {
                state->registry.destroy(entity);
            }
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

void PlayStop(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (auto* playerController = state->registry.ctx().find<PhysicsPlayerController>()) {
        playerController->Shutdown(ctx->physicsSystem);
        state->registry.ctx().erase<PhysicsPlayerController>();
    }

    PhysicsOnPlayStop(ctx, state);

    DeserializeAll(state, state->editor.pieSnapshot);
    state->editor.pieSnapshot.Clear();

    state->bIsPlaying = false;
    state->bGameCursorCaptured = false;
    ctx->setCursorHiddenFn(false);

    {
        auto camView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
        auto camEntity = camView.front();
        if (camEntity != entt::null) {
            auto& transform = state->registry.get<Component::TransformComponent>(camEntity);
            transform.translation = state->editor.pieCameraTranslation;
            transform.rotation = state->editor.pieCameraRotation;
        }
    }
}
} // Game
