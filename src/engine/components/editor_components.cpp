//
// Created by William on 2026-03-16.
//

#include "editor_components.h"

#include <fmt/format.h>

#include "component_types.h"
#include "core/containers/arena_vector.h"
#include "engine/include/engine_context.h"
#include "engine/components/component_editor.h"

namespace Engine::Component
{

Engine::ComponentEditorResult EntityFolderComponent::DrawEditor(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Engine::EditContext& edit, const char* name)
{
    const auto& comp = edit.Get<EntityFolderComponent>();
    bool open = ImGui::CollapsingHeader("Folder##entityfolder", ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
        const char* current = "(None)";
        auto anchorView = registry.View<SceneFolderComponent>();
        for (auto a : anchorView) {
            if (anchorView.get<SceneFolderComponent>(a).folderId == comp.folderId) {
                current = anchorView.get<SceneFolderComponent>(a).name.c_str();
                break;
            }
        }

        ImGui::Text("Folder");
        ImGui::SetNextItemWidth(-1);
        const bool bMixed = edit.IsMixed(&EntityFolderComponent::folderId);
        if (ImGui::BeginCombo("##entity_folder", bMixed ? "--" : comp.folderId.IsValid() ? current : "(None)")) {
            if (ImGui::Selectable("(None)", !comp.folderId.IsValid())) {
                edit.Set(&EntityFolderComponent::folderId, StringID());
            }
            for (auto a : anchorView) {
                const auto& fc = anchorView.get<SceneFolderComponent>(a);
                Core::ShortString label;
                if (fc.parentFolder.IsValid()) { label.Append("    "); }
                label.Append(fc.name);
                if (ImGui::Selectable(label.c_str(), comp.folderId == fc.folderId)) {
                    edit.Set(&EntityFolderComponent::folderId, fc.folderId);
                }
            }
            ImGui::EndCombo();
        }
    }
    return {};
}

bool SceneFolderComponent::CanAdd(const Engine::EngineContext* ctx, const Engine::EngineState* state, const Engine::SystemRegistry& registry, entt::entity entity)
{
    return false;
}

} // Engine::Component
