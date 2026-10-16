//
// Created by William on 2026-01-30.
//

#include "physics_components.h"

#include <imgui.h>

#include "engine/components/component_editor.h"
#include "engine/editor/edit_widgets.h"

namespace Engine
{
Engine::ComponentEditorResult Component::ConveyorComponent::DrawEditor(Core::ViewFamily& viewFamily, EditContext& edit, const char* name)
{
    bool open = ImGui::CollapsingHeader("Conveyor", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteconveyor");
    ImGui::PopStyleColor();

    if (open) {
        static constexpr const char* MODE_LABELS[] = {"Linear", "Circular"};
        EditWidgets::Checkbox(edit, "Enabled##conveyor", &ConveyorComponent::bEnabled);
        EditWidgets::Combo(edit, "Mode##conveyor", &ConveyorComponent::mode, MODE_LABELS, 2);
        EditWidgets::DragFloat(edit, "Speed##conveyor", &ConveyorComponent::speed, 0.05f, 0.0f, 0.0f, "%.2f m/s");
        if (edit.Get<ConveyorComponent>().mode == Mode::Linear) {
            EditWidgets::DragFloat3(edit, "Direction##conveyor", &ConveyorComponent::direction, 0.01f);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Local space; normalized when applied."); }
        }
        else {
            EditWidgets::DragFloat3(edit, "Axis##conveyor", &ConveyorComponent::axis, 0.01f);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Local space, through the entity origin. Positive speed turns counter-clockwise seen from the axis tip."); }
            EditWidgets::DragFloat(edit, "Reference Radius##conveyor", &ConveyorComponent::referenceRadius, 0.05f, 0.01f, 10000.0f, "%.2f m");
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Speed is measured at this distance from the axis; the surface turns rigidly, so it runs faster outside and slower inside."); }
        }
    }

    return {.bRequestRemoval = remove};
}
}
