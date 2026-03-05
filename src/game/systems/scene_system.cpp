//
// Created by William on 2026-02-26.
//

#include "scene_system.h"

#include <algorithm>
#include <fstream>

#include <json/nlohmann/json.hpp>

#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/components/common_components.h"
#include "game/components/core_components.h"
#include "game/components/render_components.h"
#include "game/components/scene_components.h"
#include "platform/paths.h"

namespace Game
{
Scene SaveScene(ComponentRegistry& componentRegistry, entt::registry& registry, StringID sceneId, std::string_view sceneName)
{
    Scene outScene{};
    nlohmann::json& scene = outScene.content;

    scene["scene_id"] = sceneId.id;
    scene["scene_name"] = sceneName;
    scene["entities"] = nlohmann::json::array();

    auto view = registry.view<Component::SceneComponent>();
    for (auto entity : view) {
        auto& tag = view.get<Component::SceneComponent>(entity);
        if (tag.sceneId != sceneId) continue;

        nlohmann::json entityJson;

        for (ComponentEntry& entry : componentRegistry.registry) {
            if (entry.has(registry, entity)) {
                nlohmann::json compJson;
                entry.serialize(registry, entity, compJson);
                entityJson[std::to_string(entry.typeId.id)] = compJson;
            }
        }

        scene["entities"].push_back(entityJson);
    }

    return outScene;
}

std::string LoadScene(ComponentRegistry& componentRegistry, entt::registry& registry, Scene& scene)
{
    StringID sceneId = StringID(scene.content["scene_id"].get<uint64_t>());

    std::vector<entt::entity> sceneEntities;
    sceneEntities.reserve(scene.content["entities"].size());

    // Deserialize
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

    // Post-Deserialize Effects
    for (auto entity : sceneEntities) {
        for (auto& entry : componentRegistry.registry) {
            if (entry.has(registry, entity)) {
                entry.onAddComponent(registry, entity);
            }
        }
    }

    return scene.content["scene_name"];
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
        for (auto& entry : state->componentRegistry.registry) {
            if (entry.has(state->registry, entity)) {
                entry.onRemoveComponent(state->registry, entity);
            }
        }
        state->registry.destroy(entity);
    }

    std::erase_if(state->selectedEntities, [&](entt::entity e) {
        return std::ranges::find(toDestroy, e) != toDestroy.end();
    });
}

void SaveSceneToFile(Engine::GameState* state, Engine::AssetManager* assetManager)
{
    const auto& sceneReg = assetManager->GetSceneRegistry();
    std::filesystem::path path;

    auto it = sceneReg.find(state->currentSceneId);
    if (it != sceneReg.end()) {
        path = it->second;
    }
    else {
        std::string stem = state->currentSceneName;
        std::ranges::transform(stem, stem.begin(), ::tolower);
        std::ranges::replace(stem, ' ', '_');
        path = Platform::GetAssetPath() / "scenes" / (stem + ".wscene");
        assetManager->RegisterScene(path);
    }

    Scene s = SaveScene(state->componentRegistry, state->registry, state->currentSceneId, state->currentSceneName);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << s.content.dump(2);

    LOG_INFO(Game, "Saved scene '{}' to '{}'", state->currentSceneName, path.string());
}

bool LoadSceneFromFile(Engine::GameState* state, Engine::AssetManager* assetManager, StringID sceneId)
{
    const auto& sceneReg = assetManager->GetSceneRegistry();
    auto it = sceneReg.find(sceneId);
    if (it == sceneReg.end()) {
        LOG_ERROR(Game, "Scene ID not found in registry");
        return false;
    }

    const std::filesystem::path& path = it->second;
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR(Game, "Failed to open scene file '{}'", path.string());
        return false;
    }

    Scene s;
    s.content = nlohmann::json::parse(file);
    state->currentSceneName = LoadScene(state->componentRegistry, state->registry, s);
    state->currentSceneId = StringID(s.content["scene_id"].get<uint64_t>());

    LOG_INFO(Game, "Loaded scene '{}' from '{}'", state->currentSceneName, path.string());
    return true;
}

std::vector<entt::entity> SpawnModel(Engine::GameState* state, Engine::AssetManager* assetManager, StringID modelId, const glm::vec3& offset)
{
    const Engine::AssetManager::CachedModelMetadata* cached = assetManager->GetModelMetadata(modelId);
    if (!cached) {
        LOG_ERROR(Game, "SpawnModel: '{}' not in asset registry", modelId.ToString());
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

    auto meshEntryIt = state->componentRegistry.registryMapping.find(TypeSID<Component::StaticMeshComponent>());
    assert(meshEntryIt != state->componentRegistry.registryMapping.end());
    ComponentEntry& meshEntry = state->componentRegistry.registry[meshEntryIt->second];

    std::vector<entt::entity> spawned;

    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (node.meshIndex == ~0u) continue;

        entt::entity entity = CreateSceneEntity(state);

        if (!node.name.empty()) {
            state->registry.get<Component::NameComponent>(entity).name = node.name;
        }

        auto& transform = state->registry.get<Component::TransformComponent>(entity);
        transform.translation = worldT[i] + offset;
        transform.rotation = worldR[i];
        transform.scale = worldS[i];

        auto& meshComp = state->registry.emplace<Component::StaticMeshComponent>(entity);
        meshComp.modelId = modelId;
        meshComp.meshIndex = static_cast<int32_t>(node.meshIndex);
        meshComp.modelFlags = {1.0f, 1.0f, 0.0f, 0.0f};

        meshEntry.onAddComponent(state->registry, entity);

        spawned.push_back(entity);
    }

    return spawned;
}

entt::entity CreateSceneEntity(Engine::GameState* state)
{
    entt::entity newEntity = state->registry.create();
    state->registry.emplace<Component::TransformComponent>(newEntity);
    state->registry.emplace<Component::SceneComponent>(newEntity, state->currentSceneId);
    Component::StableIdComponent stableIdComponent = state->registry.emplace<Component::StableIdComponent>(newEntity, Component::StableIdComponent::Generate(state->rng));
    state->stableIdToEntityMap[stableIdComponent.id] = newEntity;
    static int32_t runningNameTally = 0;
    auto newName = fmt::format("New Entity {}", runningNameTally++);
    state->registry.emplace<Component::NameComponent>(newEntity, newName);
    return newEntity;
}
} // Game
