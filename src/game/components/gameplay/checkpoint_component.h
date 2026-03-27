//
// Created by William on 2026-03-27.
//

#ifndef WILL_ENGINE_CHECKPOINT_COMPONENT_H
#define WILL_ENGINE_CHECKPOINT_COMPONENT_H

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "core/string_id.h"
#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct CheckpointComponent
{
    StringID checkpointId{};

    static void OnConstruct(entt::registry& registry, entt::entity entity);
    static void Serialize(const CheckpointComponent& comp, nlohmann::json& json);
    static void Deserialize(CheckpointComponent& comp, const nlohmann::json& json);
    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_CHECKPOINT_COMPONENT_H
