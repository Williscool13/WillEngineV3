//
// Created by William on 2026-03-23.
//

#include "player_spawn_component.h"

#include "imgui.h"

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "engine/components/component_editor.h"


namespace Game
{

Engine::ComponentEditorResult Component::PlayerSpawnComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    bool open = ImGui::CollapsingHeader("Player Spawn", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteplayerspawn");
    ImGui::PopStyleColor();

    if (open) {
        Engine::EditWidgets::DragInt(edit, "Priority", &PlayerSpawnComponent::priority);
        Engine::EditWidgets::DragFloat3(edit, "Offset", &PlayerSpawnComponent::offset, 0.1f);
    }
    return {.bRequestRemoval = remove};
}

}
