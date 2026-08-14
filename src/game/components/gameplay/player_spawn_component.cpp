//
// Created by William on 2026-03-23.
//

#include "player_spawn_component.h"

#include "imgui.h"

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/component-registry/component_editor.h"

void Game::Component::PlayerSpawnComponent::Serialize(const PlayerSpawnComponent& comp, Engine::TextWriter& w)
{
    static const PlayerSpawnComponent DEF{};
    w.KeyOpt("priority", comp.priority, DEF.priority);
    w.KeyOpt("offset", comp.offset, DEF.offset);
}

void Game::Component::PlayerSpawnComponent::Deserialize(PlayerSpawnComponent& comp, const Engine::TextReader& r)
{
    comp.priority = r.Int("priority", comp.priority);
    comp.offset = r.Vec3("offset", comp.offset);
}

namespace Game
{

Engine::ComponentEditorResult Component::PlayerSpawnComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<Component::PlayerSpawnComponent>(entity);
    bool open = ImGui::CollapsingHeader("Player Spawn", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteplayerspawn");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        modified |= ImGui::DragInt("Priority", &component.priority);
        modified |= ImGui::DragFloat3("Offset", &component.offset.x, 0.1f);
    }
    return {.bRequestRemoval = remove, .bModified = modified};
}

}
