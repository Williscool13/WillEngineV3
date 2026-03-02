//
// Created by William on 2026-02-26.
//

#include "scene_system.h"

#include <json/nlohmann/json.hpp>

#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/components/scene_components.h"

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
