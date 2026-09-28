//
// Created by William on 2026-03-27.
//

#include "checkpoint_component.h"

#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "engine/components/component_editor.h"
#include "engine/components/core_components.h"
#include "game/fwd_components.h"

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


Engine::ComponentEditorResult CheckpointComponent::DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name)
{
    entt::registry& registry = edit.Registry();
    const entt::entity entity = edit.Primary();
    const auto& component = edit.Get<CheckpointComponent>();
    bool open = ImGui::CollapsingHeader("Checkpoint", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletecheckpoint");
    ImGui::PopStyleColor();

    if (open) {
        const auto idLabel = Core::InlineString<64>::Format("ID: %llu", component.checkpointId.id);
        ImGui::TextUnformatted(edit.IsMixed(&CheckpointComponent::checkpointId) ? "ID: --" : idLabel.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50.f);
        if (ImGui::SmallButton("Regenerate")) {
            Engine::EngineState* state = edit.State();
            edit.Modify<CheckpointComponent>([state](CheckpointComponent& c) { c.checkpointId = StringID(state->rng()); });
        }
        Engine::EditWidgets::DragInt(edit, "Priority", &CheckpointComponent::priority);
        Engine::EditWidgets::DragFloat3(edit, "Spawn Offset", &CheckpointComponent::spawnOffset, 0.1f);
        Engine::EditWidgets::DragFloat3(edit, "Spawn Rotation", &CheckpointComponent::spawnRotation, 0.5f);
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

    return {.bRequestRemoval = remove};
}
} // Game::Component

