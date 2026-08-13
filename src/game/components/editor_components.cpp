//
// Created by William on 2026-03-16.
//

#include "editor_components.h"

#include <fmt/format.h>
#include <json/nlohmann/json.hpp>

#include "component_types.h"
#include "core/containers/arena_vector.h"
#include "engine/include/engine_context.h"
#include "game/component-registry/component_editor.h"

namespace Game::Component
{
void EntityFolderComponent::Serialize(const EntityFolderComponent& comp, nlohmann::json& json)
{
    json["folderId"] = comp.folderId.id;
}

void EntityFolderComponent::Deserialize(EntityFolderComponent& comp, const nlohmann::json& json)
{
    if (json.contains("folderId")) {
        comp.folderId = StringID(json["folderId"].get<uint64_t>());
    }
}

Engine::ComponentEditorResult EntityFolderComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity,
                                                        const char* name)
{
    auto& comp = registry.get<EntityFolderComponent>(entity);
    bool open = ImGui::CollapsingHeader("Folder##entityfolder", ImGuiTreeNodeFlags_DefaultOpen);

    bool modified = false;
    if (open) {
        const char* current = "(None)";
        auto anchorView = registry.view<SceneFolderComponent>();
        for (auto a : anchorView) {
            if (anchorView.get<SceneFolderComponent>(a).folderId == comp.folderId) {
                current = anchorView.get<SceneFolderComponent>(a).name.c_str();
                break;
            }
        }

        ImGui::Text("Folder");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##entity_folder", comp.folderId.IsValid() ? current : "(None)")) {
            if (ImGui::Selectable("(None)", !comp.folderId.IsValid())) {
                comp.folderId = StringID();
                modified = true;
            }
            for (auto a : anchorView) {
                const auto& fc = anchorView.get<SceneFolderComponent>(a);
                Core::ShortString label;
                if (fc.parentFolder.IsValid()) { label.Append("    "); }
                label.Append(fc.name);
                if (ImGui::Selectable(label.c_str(), comp.folderId == fc.folderId)) {
                    comp.folderId = fc.folderId;
                    modified = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    return {.bModified = modified};
}

bool SceneFolderComponent::CanAdd(const entt::registry& registry, entt::entity entity)
{
    return false;
}

void SceneFolderComponent::Serialize(const SceneFolderComponent& comp, nlohmann::json& json)
{
    json["folderId"] = comp.folderId.id;
    json["parentFolder"] = comp.parentFolder.id;
    json["name"] = comp.name.c_str();
}

void SceneFolderComponent::Deserialize(SceneFolderComponent& comp, const nlohmann::json& json)
{
    if (json.contains("folderId")) {
        comp.folderId = StringID(json["folderId"].get<uint64_t>());
    }
    if (json.contains("parentFolder")) {
        comp.parentFolder = StringID(json["parentFolder"].get<uint64_t>());
    }
    if (json.contains("name")) {
        comp.name = Core::ShortString(json["name"].get<std::string_view>());
    }
}
} // Game::Component
