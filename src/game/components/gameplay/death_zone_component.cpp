//
// Created by William on 2026-03-28.
//

#include "death_zone_component.h"

#include <imgui.h>

#include "game/component-registry/component_editor.h"

namespace Game
{
Engine::ComponentEditorResult Component::DeathZoneComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Death Zone", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletedeathzone");
    ImGui::PopStyleColor();

    if (open) {
        ImGui::TextDisabled("Teleports player to active checkpoint on contact.");
    }
    return {.bRequestRemoval = remove};
}
} // Game
