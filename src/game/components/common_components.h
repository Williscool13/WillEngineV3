//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_COMMON_COMPONENTS_H
#define WILL_ENGINE_COMMON_COMPONENTS_H

#include <random>

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "core/string_id.h"
#include "core/containers/inline_string.h"
#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct NameComponent
{
    Core::InlineString<256> name;

    static void Serialize(const NameComponent& comp, nlohmann::json& json);
    static void Deserialize(NameComponent& comp, const nlohmann::json& json);
    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

struct DoNotSerializeTag
{};

struct PrefabInstanceComponent
{
    StringID prefabId;
    bool bMasterPrefab{false};

    static void Serialize(const PrefabInstanceComponent& comp, nlohmann::json& json);
    static void Deserialize(PrefabInstanceComponent& comp, const nlohmann::json& json);
    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_COMMON_COMPONENTS_H
