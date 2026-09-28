//
// Created by William on 2026-03-16.
//

#ifndef WILL_ENGINE_EDITOR_COMPONENTS_H
#define WILL_ENGINE_EDITOR_COMPONENTS_H
#include <array>

#include <entt/entt.hpp>

#include "core/containers/inline_string.h"
#include "core/string_id.h"
#include "engine/component_registry.h"
#include "engine/reflection/reflection.h"

namespace Core { struct ViewFamily; }

namespace Engine::Component
{
struct EntityFolderComponent
{
    static constexpr const char* COMPONENT_NAME = "EntityFolderComponent";

    /**
     * Immediate containing folder, referencing a SceneFolderComponent::folderId.
     * Invalid (or pointing at a missing anchor) means the entity sits at the scene root.
     */
    StringID folderId;

    WILL_REFLECT(EntityFolderComponent,
        WILL_FIELD(folderId))

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
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

    WILL_REFLECT(SceneFolderComponent,
        WILL_FIELD(folderId),
        WILL_FIELD(parentFolder),
        WILL_FIELD(name))

    static bool CanAdd(const entt::registry& registry, entt::entity entity);
};
}

#endif //WILL_ENGINE_EDITOR_COMPONENTS_H
