//
// Created by William on 2026-03-23.
//

#ifndef WILL_ENGINE_PLAYER_SPAWN_COMPONENT_H
#define WILL_ENGINE_PLAYER_SPAWN_COMPONENT_H

#include <glm/vec3.hpp>
#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "engine/engine_api.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct PlayerSpawnComponent
{
    int32_t priority{0};
    glm::vec3 offset{0.0f, 0.0f, 0.0f};

    static void Serialize(const PlayerSpawnComponent& comp, nlohmann::json& json);
    static void Deserialize(PlayerSpawnComponent& comp, const nlohmann::json& json);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_PLAYER_SPAWN_COMPONENT_H
