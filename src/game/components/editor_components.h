//
// Created by William on 2026-03-16.
//

#ifndef WILL_ENGINE_EDITOR_COMPONENTS_H
#define WILL_ENGINE_EDITOR_COMPONENTS_H
#include <array>

#include <entt/entt.hpp>
#include <json/nlohmann/json_fwd.hpp>

#include "core/containers/inline_string.h"
#include "core/string_id.h"
#include "engine/component_registry.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct EntityFolderComponent
{
    static constexpr const char* COMPONENT_NAME = "EntityFolderComponent";

    /**
     * Immediate containing folder, referencing a SceneFolderComponent::folderId.
     * Invalid (or pointing at a missing anchor) means the entity sits at the scene root.
     */
    StringID folderId;

    static void Serialize(const EntityFolderComponent& comp, nlohmann::json& json);
    static void Deserialize(EntityFolderComponent& comp, const nlohmann::json& json);
    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};

/**
 * Scene-hierarchy folder anchor. Lives as its own (gameplay-less) scene entity so a folder persists even with no members.
 * Identity (folderId) is a stable random id decoupled from the display name, so folders can be renamed without orphaning their members.
 */
struct SceneFolderComponent
{
    static constexpr const char* COMPONENT_NAME = "SceneFolderComponent";

    StringID folderId;
    StringID parentFolder;
    Core::ShortString name;

    static bool CanAdd(const entt::registry& registry, entt::entity entity);
    static void Serialize(const SceneFolderComponent& comp, nlohmann::json& json);
    static void Deserialize(SceneFolderComponent& comp, const nlohmann::json& json);
};
}

#endif //WILL_ENGINE_EDITOR_COMPONENTS_H
