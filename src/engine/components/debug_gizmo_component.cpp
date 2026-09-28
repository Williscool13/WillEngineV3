//
// Created by William on 2026-03-23.
//

#include "debug_gizmo_component.h"

#include "imgui.h"

#include "engine/components/component_editor.h"

namespace
{
constexpr const char* SHAPE_NAMES[] = {"None", "Sphere", "Box"};
static_assert(std::size(SHAPE_NAMES) == static_cast<size_t>(Engine::Component::DebugGizmoShape::Count));
}


namespace Engine
{

Engine::ComponentEditorResult Component::DebugGizmoComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    bool open = ImGui::CollapsingHeader("Debug Gizmo", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletedebuggizmo");
    ImGui::PopStyleColor();

    if (open) {
        EditWidgets::Combo(edit, "Shape", &DebugGizmoComponent::shape, SHAPE_NAMES, IM_ARRAYSIZE(SHAPE_NAMES));
        EditWidgets::DragFloat3(edit, "Extents", &DebugGizmoComponent::extents, 0.01f, 0.0f, 100.0f);
        EditWidgets::ColorEdit4(edit, "Color", &DebugGizmoComponent::color);
        EditWidgets::DragFloat(edit, "Line Width", &DebugGizmoComponent::lineWidth, 0.005f, 0.01f, 1.0f);
    }
    return {.bRequestRemoval = remove};
}

}
