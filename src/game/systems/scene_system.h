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

namespace Engine
{
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
LoadSceneResult LoadScene(Engine::AssetManager* assetManager, Core::ComponentRegistry& componentRegistry, entt::registry& registry, Scene& scene);
} // Game

#endif //WILL_ENGINE_SCENE_SYSTEM_H