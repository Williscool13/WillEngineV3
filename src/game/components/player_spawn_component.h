//
// Created by William on 2026-03-23.
//

#ifndef WILL_ENGINE_PLAYER_SPAWN_COMPONENT_H
#define WILL_ENGINE_PLAYER_SPAWN_COMPONENT_H

#include <glm/vec3.hpp>
#include <entt/entt.hpp>

#include "engine/component_registry.h"
#include "engine/reflection/reflection.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct PlayerSpawnComponent
{
    static constexpr const char* COMPONENT_NAME = "PlayerSpawnComponent";

    int32_t priority{0};
    glm::vec3 offset{0.0f, 0.0f, 0.0f};

    WILL_REFLECT(PlayerSpawnComponent,
        WILL_FIELD(priority),
        WILL_FIELD(offset, .speed = 0.1f))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};
}

#endif //WILL_ENGINE_PLAYER_SPAWN_COMPONENT_H
