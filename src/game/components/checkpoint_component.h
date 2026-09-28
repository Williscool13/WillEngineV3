//
// Created by William on 2026-03-27.
//

#ifndef WILL_ENGINE_CHECKPOINT_COMPONENT_H
#define WILL_ENGINE_CHECKPOINT_COMPONENT_H

#include <glm/vec3.hpp>
#include <entt/entt.hpp>

#include "core/string_id.h"
#include "engine/component_registry.h"
#include "engine/components/component_types.h"
#include "engine/reflection/reflection.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct CheckpointComponent
{
    static constexpr const char* COMPONENT_NAME = "CheckpointComponent";

    StringID checkpointId{};
    int32_t priority{0};
    glm::vec3 spawnOffset{0.0f, 0.0f, 0.0f};
    glm::vec3 spawnRotation{0.0f, 0.0f, 0.0f};

    WILL_REFLECT(CheckpointComponent,
        WILL_FIELD(checkpointId),
        WILL_FIELD(priority),
        WILL_FIELD(spawnOffset, .speed = 0.1f),
        WILL_FIELD(spawnRotation, .speed = 0.5f))

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};
}

#endif //WILL_ENGINE_CHECKPOINT_COMPONENT_H
