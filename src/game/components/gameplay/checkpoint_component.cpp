//
// Created by William on 2026-03-27.
//

#include "checkpoint_component.h"

#include <json/nlohmann/json.hpp>
#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include "engine/engine_api.h"
#include "game/component-registry/component_editor.h"
#include "game/components/core_components.h"

namespace Game::Component
{
void CheckpointComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<CheckpointComponent>(entity);
    if (comp.checkpointId.id == 0) {
        auto* state = registry.ctx().get<Engine::EngineState*>();
        comp.checkpointId = StringID(state->rng());
    }
}

void CheckpointComponent::Serialize(const CheckpointComponent& comp, nlohmann::json& json)
{
    json["checkpointId"] = comp.checkpointId.id;
    json["priority"] = comp.priority;
    json["spawnOffset"] = {comp.spawnOffset.x, comp.spawnOffset.y, comp.spawnOffset.z};
    json["spawnRotation"] = {comp.spawnRotation.x, comp.spawnRotation.y, comp.spawnRotation.z};
}

void CheckpointComponent::Deserialize(CheckpointComponent& comp, const nlohmann::json& json)
{
    comp.checkpointId = StringID(json["checkpointId"].get<uint64_t>());
    if (json.contains("priority")) {
        comp.priority = json["priority"].get<int32_t>();
    }
    if (json.contains("spawnOffset")) {
        const auto& o = json["spawnOffset"];
        comp.spawnOffset = glm::vec3(o[0].get<float>(), o[1].get<float>(), o[2].get<float>());
    }
    if (json.contains("spawnRotation")) {
        const auto& r = json["spawnRotation"];
        comp.spawnRotation = glm::vec3(r[0].get<float>(), r[1].get<float>(), r[2].get<float>());
    }
}

Engine::ComponentEditorResult CheckpointComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    auto& component = registry.get<CheckpointComponent>(entity);
    bool open = ImGui::CollapsingHeader("Checkpoint", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletecheckpoint");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        char idLabel[64];
        snprintf(idLabel, sizeof(idLabel), "ID: %llu", component.checkpointId.id);
        ImGui::TextUnformatted(idLabel);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50.f);
        if (ImGui::SmallButton("Regenerate")) {
            auto* state = registry.ctx().get<Engine::EngineState*>();
            component.checkpointId = StringID(state->rng());
            modified = true;
        }
        modified |= ImGui::DragInt("Priority", &component.priority);
        modified |= ImGui::DragFloat3("Spawn Offset", &component.spawnOffset.x, 0.1f);
        modified |= ImGui::DragFloat3("Spawn Rotation", &component.spawnRotation.x, 0.5f);
    }

#ifdef WDEBUG
    {
        glm::vec3 basePos{0.0f};
        if (auto* transform = registry.try_get<TransformComponent>(entity)) {
            basePos = transform->translation;
        }
        const glm::vec3 spawnPos = basePos + component.spawnOffset;

        constexpr glm::vec4 kSpawnColor{0.2f, 1.0f, 0.3f, 1.0f};
        constexpr float kSphereRadius = 0.15f;
        constexpr float kArrowLength = 1.0f;

        DEBUG_ADD_SPHERE(viewFamily.debugSpheres, Core::DebugSphere{
            .center = spawnPos,
            .radius = kSphereRadius,
            .color = kSpawnColor,
        });

        const glm::quat rot = glm::quat(glm::radians(component.spawnRotation));
        const glm::vec3 forward = rot * glm::vec3(0.0f, 0.0f, -1.0f);
        DEBUG_ADD_LINE(viewFamily.debugLines, Core::DebugLine{
            .start = spawnPos,
            .end = spawnPos + forward * kArrowLength,
            .color = kSpawnColor,
        });
    }
#endif

    return {.bRequestRemoval = remove, .bModified = modified};
}
} // Game::Component

