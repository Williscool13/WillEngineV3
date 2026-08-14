//
// Created by William on 2026-03-27.
//

#include "checkpoint_component.h"

#include <imgui.h>
#include <glm/gtc/quaternion.hpp>

#include "engine/engine_api.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
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

void CheckpointComponent::Serialize(const CheckpointComponent& comp, Engine::TextWriter& w)
{
    static const CheckpointComponent DEF{};
    w.KeyOpt("checkpointId", comp.checkpointId.id, uint64_t{0});
    w.KeyOpt("priority", comp.priority, DEF.priority);
    w.KeyOpt("spawnOffset", comp.spawnOffset, DEF.spawnOffset);
    w.KeyOpt("spawnRotation", comp.spawnRotation, DEF.spawnRotation);
}

void CheckpointComponent::Deserialize(CheckpointComponent& comp, const Engine::TextReader& r)
{
    comp.checkpointId = StringID(r.U64("checkpointId", comp.checkpointId.id));
    comp.priority = r.Int("priority", comp.priority);
    comp.spawnOffset = r.Vec3("spawnOffset", comp.spawnOffset);
    comp.spawnRotation = r.Vec3("spawnRotation", comp.spawnRotation);
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

