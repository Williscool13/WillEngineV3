//
// Created by William on 2026-03-16.
//

#ifndef WILL_ENGINE_EDITOR_COMPONENTS_H
#define WILL_ENGINE_EDITOR_COMPONENTS_H
#include <array>

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "../../core/containers/inline_string.h"
#include "core/string_id.h"
#include "engine/engine_api.h"
#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct EntityFolderComponent
{
    /**
     * Up to 2 levels deep
     */
    std::array<StringID, 2> folderHierarchy;

    /**
     * Stack-based strings (max 16 char)
     */
    std::array<Core::ShortString, 2> folderHierarchyNames;

    static void Serialize(const EntityFolderComponent& comp, nlohmann::json& json);
    static void Deserialize(EntityFolderComponent& comp, const nlohmann::json& json);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_EDITOR_COMPONENTS_H
