//
// Created by William on 2026-02-26.
//

#include "scene_system.h"

#include <json/nlohmann/json.hpp>

#include "physics_system.h"
#include "core/component_registry.h"
#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/components/components.h"
#include "game/components/core_components.h"
#include "game/components/physics_components.h"
#include "game/components/render_components.h"
#include "game/components/scene_components.h"
#include "physics/physics_system.h"
#include "platform/paths.h"

namespace Game::System
{
Scene SaveScene(Core::ComponentRegistry& componentRegistry, entt::registry& registry, StringID sceneId)
{
    Scene outScene{};
    nlohmann::json& scene = outScene.content;

    scene["scene_id"] = sceneId.id;
    scene["entities"] = nlohmann::json::array();

    auto view = registry.view<Component::SceneComponent>();
    for (auto entity : view) {
        auto& tag = view.get<Component::SceneComponent>(entity);
        if (tag.sceneId != sceneId) continue;

        nlohmann::json entityJson;

        for (Core::ComponentEntry& entry : componentRegistry.registry) {
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

LoadSceneResult LoadScene(Core::EngineContext* ctx, Engine::GameState* gameState, Core::ComponentRegistry& componentRegistry, entt::registry& registry, Scene& scene)
{
    StringID sceneId = StringID(scene.content["scene_id"].get<uint64_t>());

    std::vector<entt::entity> sceneEntities;
    sceneEntities.reserve(scene.content["entities"].size());

    // Deserialize
    for (auto& entityJson : scene.content["entities"]) {
        auto entity = registry.create();
        for (auto& [key, compJson] : entityJson.items()) {
            uint64_t typeId = std::stoull(key);
            for (Core::ComponentEntry& entry : componentRegistry.registry) {
                if (entry.typeId.id == typeId) {
                    entry.deserialize(registry, entity, compJson);
                    break;
                }
            }
        }
        registry.emplace<Component::SceneComponent>(entity, sceneId);
        sceneEntities.push_back(entity);
    }

    // Add transient components
    bool bHasPendingModelLoads = false;
    std::unordered_map<StringID, Engine::WillModelHandle> modelIdToHandle;

    for (auto entity : sceneEntities) {
        // Transform Cache
        if (auto* transform = registry.try_get<Component::TransformComponent>(entity)) {
            glm::mat4 m = Component::GetMatrix(*transform);
            registry.emplace_or_replace<Component::RenderTransformComponent>(entity, m, m);
            registry.emplace_or_replace<Component::DirtyRenderTransformTag>(entity);
        }

        // Static Mesh Loading
        if (auto* mesh = registry.try_get<Component::StaticMeshComponent>(entity)) {
            if (!modelIdToHandle.contains(mesh->modelId)) {
                modelIdToHandle[mesh->modelId] = ctx->assetManager->LoadModel(mesh->modelId);
            }
            mesh->modelHandle = modelIdToHandle[mesh->modelId];
            registry.emplace_or_replace<Component::StaticMeshLoadingTag>(entity);
            bHasPendingModelLoads = true;
        }

        // Physics Loading
        if (auto* bodyDesc = registry.try_get<Component::PhysicsBodyDesc>(entity)) {
            if (auto* transform = registry.try_get<Component::TransformComponent>(entity)) {
                JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();
                JPH::RVec3 pos(transform->translation.x, transform->translation.y, transform->translation.z);
                JPH::Quat rot(transform->rotation.x, transform->rotation.y, transform->rotation.z, transform->rotation.w);
                auto bodyId = CreateBodyFromDesc(bodyInterface, *bodyDesc, pos, rot);
                registry.emplace<Component::PhysicsBodyComponent>(entity, bodyId);
                if (bodyDesc->motionType == Component::PhysicsMotionType::Dynamic) {
                    registry.emplace<Component::DynamicPhysicsBodyComponent>(entity, transform->translation, transform->rotation);
                }
            } else {
                LOG_WARN(Game, "PhysicsBodyDesc on entity without TransformComponent, skipping");
            }
        }

        if (auto* stable = registry.try_get<Component::StableIdComponent>(entity)) {
            assert(!gameState->stableIdToEntityMap.contains(stable->id) && "Duplicate stable ID detected");
            gameState->stableIdToEntityMap[stable->id] = entity;
        }
    }

    LoadSceneResult loadSceneResult{};
    loadSceneResult.sceneId = sceneId;
    loadSceneResult.bHasPendingModelLoads = bHasPendingModelLoads;
    loadSceneResult.loadedModelHandles.reserve(modelIdToHandle.size());
    for (auto& val : modelIdToHandle | std::views::values) {
        loadSceneResult.loadedModelHandles.push_back(val);
    }

    return loadSceneResult;
}
} // Game
