//
// Created by William on 2026-03-23.
//

#include "player_spawn_component.h"

#include <json/nlohmann/json.hpp>

#include "imgui.h"

#include "game/component-registry/component_editor.h"

void Game::Component::PlayerSpawnComponent::Serialize(const PlayerSpawnComponent& comp, nlohmann::json& json)
{
    json["priority"] = comp.priority;
    json["offset"] = {comp.offset.x, comp.offset.y, comp.offset.z};
}

void Game::Component::PlayerSpawnComponent::Deserialize(PlayerSpawnComponent& comp, const nlohmann::json& json)
{
    comp.priority = json["priority"].get<int32_t>();
    if (json.contains("offset")) {
        const auto& o = json["offset"];
        comp.offset = glm::vec3(o[0].get<float>(), o[1].get<float>(), o[2].get<float>());
    }
}

namespace Game
{

ComponentEditorResult Component::PlayerSpawnComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<Component::PlayerSpawnComponent>(entity);
    bool open = ImGui::CollapsingHeader("Player Spawn", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deleteplayerspawn");
    ImGui::PopStyleColor();

    if (open) {
        ImGui::DragInt("Priority", &component.priority);
        ImGui::DragFloat3("Offset", &component.offset.x, 0.1f);
    }
    return {.requestRemoval = remove};
}

}
