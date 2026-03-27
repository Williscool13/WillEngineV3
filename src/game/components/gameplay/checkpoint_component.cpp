//
// Created by William on 2026-03-27.
//

#include "checkpoint_component.h"

#include <json/nlohmann/json.hpp>
#include <imgui.h>

#include "engine/engine_api.h"
#include "game/component-registry/component_editor.h"

namespace Game::Component
{
void CheckpointComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<CheckpointComponent>(entity);
    if (comp.checkpointId.id == 0) {
        auto* state = registry.ctx().get<Engine::GameState*>();
        comp.checkpointId = StringID(state->rng());
    }
}

void CheckpointComponent::Serialize(const CheckpointComponent& comp, nlohmann::json& json)
{
    json["checkpointId"] = comp.checkpointId.id;
}

void CheckpointComponent::Deserialize(CheckpointComponent& comp, const nlohmann::json& json)
{
    comp.checkpointId = StringID(json["checkpointId"].get<uint64_t>());
}
} // Game::Component

namespace Game
{
ComponentEditorResult Component::CheckpointComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<Component::CheckpointComponent>(entity);
    bool open = ImGui::CollapsingHeader("Checkpoint", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletecheckpoint");
    ImGui::PopStyleColor();

    if (open) {
        char idLabel[64];
        snprintf(idLabel, sizeof(idLabel), "ID: %llu", component.checkpointId.id);
        ImGui::TextUnformatted(idLabel);
    }
    return {.requestRemoval = remove};
}
} // Game
