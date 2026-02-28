//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_SYSTEM_H
#define WILL_ENGINE_SCENE_SYSTEM_H
#include <entt/entt.hpp>

#include "../core/scene.h"
#include "core/component_registry.h"
#include "core/string_id.h"
#include "render/model/will_model_asset.h"

namespace Core
{
struct EngineContext;
}

namespace Engine
{
struct GameState;
class AssetManager;
}

namespace Game::System
{
struct LoadSceneResult {
    StringID sceneId;
    bool bHasPendingModelLoads;
    std::vector<Engine::WillModelHandle> loadedModelHandles;
};
Scene SaveScene(Core::ComponentRegistry& componentRegistry, entt::registry& registry, StringID sceneId);
LoadSceneResult LoadScene(Core::EngineContext* ctx, Engine::GameState* gameState, Core::ComponentRegistry& componentRegistry, entt::registry& registry, Scene& scene);
} // Game

#endif //WILL_ENGINE_SCENE_SYSTEM_H