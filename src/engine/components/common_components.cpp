//
// Created by William on 2026-02-26.
//

#include "common_components.h"

#include "imgui.h"

#include "engine/component_registry.h"
#include "engine/components/component_editor.h"

namespace Engine::Component
{

Engine::ComponentEditorResult PrefabInstanceComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    const auto& comp = edit.Get<PrefabInstanceComponent>();

    bool open = ImGui::CollapsingHeader("Prefab Instance##componentprefab", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteprefab");
    ImGui::PopStyleColor();

    if (open) {
        ImGui::TextDisabled("ID: %llu", comp.prefabId.id);
        if (!edit.IsMulti() && EditWidgets::Checkbox(edit, "Master Prefab", &PrefabInstanceComponent::bMasterPrefab) && comp.bMasterPrefab) {
            auto view = registry.view<PrefabInstanceComponent>();
            for (auto e : view) {
                if (e == entity) { continue; }
                auto& other = view.get<PrefabInstanceComponent>(e);
                if (other.prefabId == comp.prefabId) {
                    other.bMasterPrefab = false;
                }
            }
        }
    }
    return {.bRequestRemoval = remove};
}


Engine::ComponentEditorResult NameComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    if (ImGui::CollapsingHeader("Name##componentname", ImGuiTreeNodeFlags_DefaultOpen)) {
        EditWidgets::InputText(edit, "Name", &NameComponent::name);
    }
    return {};
}
} // Engine::Component
