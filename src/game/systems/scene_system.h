//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_SYSTEM_H
#define WILL_ENGINE_SCENE_SYSTEM_H
#include <entt/entt.hpp>

#include "game/scene/scene.h"
#include "game/components/component_registry.h"
#include "core/string_id.h"

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
} // Game

#endif //WILL_ENGINE_SCENE_SYSTEM_H