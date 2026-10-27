//
// Created by William on 2026-10-22.
//

#include "edit_widgets.h"

#include "engine/engine_api.h"
#include "engine/components/common_components.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/systems/scene_system.h"

namespace Engine::EditWidgets
{
bool EntityRefPicker(EngineState* state, const char* label, EntityRef& ref, bool bMixed, Engine::SystemRegistry& registry)
{
    const entt::entity target = FindEntity(state, ref.stableId, registry);
    const bool bMissing = !bMixed && ref.stableId.IsValid() && target == entt::null;

    using Text = Core::InlineString<160>;
    Text text;
    if (bMixed) {
        text = Text("--");
    }
    else if (!ref.stableId.IsValid()) {
        text = Text("None");
    }
    else if (bMissing) {
        text = Text::Format("Missing (%016llx)", static_cast<unsigned long long>(ref.stableId.id));
    }
    else {
        const auto* name = registry.TryGet<Component::NameComponent>(target);
        text = Text(name ? name->name.c_str() : "(unnamed)");
    }

    bool bChanged = false;
    ImGui::PushID(label);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float clearWidth = ImGui::GetFrameHeight();
    if (bMissing) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); }
    ImGui::Button(text.c_str(), ImVec2(ImGui::CalcItemWidth() - clearWidth - style.ItemInnerSpacing.x, 0.0f));
    if (bMissing) { ImGui::PopStyleColor(); }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY")) {
            const entt::entity dropped = *static_cast<const entt::entity*>(payload->Data);
            if (const auto* id = registry.Valid(dropped) ? registry.TryGet<Component::StableIdComponent>(dropped) : nullptr) {
                ref = EntityRef{id->id};
                bChanged = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (ImGui::Button("X", ImVec2(clearWidth, 0.0f)) && ref.stableId.IsValid()) {
        ref = EntityRef{};
        bChanged = true;
    }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted(label, ImGui::FindRenderedTextEnd(label));
    ImGui::PopID();
    return bChanged;
}
} // Engine::EditWidgets
