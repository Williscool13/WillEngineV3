//
// Created by William on 2026-03-16.
//

#include "editor_components.h"

#include "component_types.h"
#include "game/component-registry/component_copy.h"
#include "game/component-registry/component_serialization.h"
#include "game/component-registry/component_initialization.h"
#include "game/component-registry/component_editor.h"

namespace Game
{
template<>
void SerializeComponent<Component::EntityFolderComponent>(const Component::EntityFolderComponent& comp, nlohmann::json& json)
{
    json["folderHierarchyNames"] = nlohmann::json::array();
    for (const ShortString& name : comp.folderHierarchyNames) {
        json["folderHierarchyNames"].push_back(name.c_str());
    }
}

template<>
void DeserializeComponent<Component::EntityFolderComponent>(Component::EntityFolderComponent& comp, const nlohmann::json& json)
{
    if (json.contains("folderHierarchyNames")) {
        const auto& arr = json["folderHierarchyNames"];
        for (size_t i = 0; i < comp.folderHierarchyNames.size() && i < arr.size(); ++i) {
            comp.folderHierarchyNames[i] = ShortString(arr[i].get<std::string>().c_str());
            const std::string s = arr[i].get<std::string>();
            comp.folderHierarchy[i] = StringID(s.c_str(), s.size());
        }
    }
}
template<>
ComponentEditorResult DrawComponentEditor<Component::EntityFolderComponent>(Component::EntityFolderComponent& component, Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
    const char* name)
{
    // todo: select what folder hierarchy. select dropdown from existing folders and also add the ability to create a new folder name
    return {};
}
}
