//
// Created by William on 2026-08-03.
//

#include "local_ddgi_volume_component.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"
#include "game/components/core_components.h"
#include "engine/include/engine_context.h"
#include "game/input/game_actions.h"
#include "render/interface/render_interface.h"

namespace Game::Component
{
Engine::ComponentEditorResult LocalDDGIVolumeComponent::DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name)
{
    bool open = ImGui::CollapsingHeader("Local DDGI Volume", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletelocalddgi");
    ImGui::PopStyleColor();

    auto* transform = registry.try_get<TransformComponent>(entity);

    bool modified = false;
    if (open) {
        auto& comp = registry.get<LocalDDGIVolumeComponent>(entity);

        modified |= ImGui::Checkbox("Enabled##lddgi", &comp.bEnabled);
        if (ImGui::DragFloat("Probe Spacing##lddgi", &comp.probeSpacing, 0.01f, 0.25f, 2.0f, "%.2f")) {
            comp.probeSpacing = glm::clamp(comp.probeSpacing, 0.25f, 2.0f);
            modified = true;
        }

        const float extent = static_cast<float>(Core::LOCAL_DDGI_PROBES_PER_AXIS - 1) * comp.probeSpacing;
        ImGui::Text("Window: %.2f m cube (%d probes)", extent, Core::LOCAL_DDGI_PROBES_PER_AXIS * Core::LOCAL_DDGI_PROBES_PER_AXIS * Core::LOCAL_DDGI_PROBES_PER_AXIS);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Translation = min corner (snapped to spacing); rotation and scale ignored. Spacing is the only size control; tile volumes to cover a larger room.");
    }

    if (transform && open) {
        const auto& comp = registry.get<LocalDDGIVolumeComponent>(entity);
        const float spacing = comp.probeSpacing;
        const Vec3 windowMin = glm::round(transform->translation / spacing) * spacing;
        const Vec3 windowMax = windowMin + Vec3(static_cast<float>(Core::LOCAL_DDGI_PROBES_PER_AXIS - 1) * spacing);

        constexpr Vec4 windowColor{0.95f, 0.9f, 0.35f, 1.0f};
        constexpr float lineWidth = 0.02f;
        DEBUG_ADD_BOX(viewFamily.debugBoxes, {(windowMin + windowMax) * 0.5f, (windowMax - windowMin) * 0.5f, Quat{1.0f, 0.0f, 0.0f, 0.0f}, windowColor, lineWidth});
    }

    return {.bRequestRemoval = remove, .bModified = modified};
}

void LocalDDGIVolumeComponent::Serialize(const LocalDDGIVolumeComponent& comp, Engine::TextWriter& w)
{
    static const LocalDDGIVolumeComponent DEF{};
    w.KeyOpt("volumeId", comp.volumeId, DEF.volumeId);
    w.KeyOpt("bEnabled", comp.bEnabled, DEF.bEnabled);
    w.KeyOpt("probeSpacing", comp.probeSpacing, DEF.probeSpacing);
}

void LocalDDGIVolumeComponent::Deserialize(LocalDDGIVolumeComponent& comp, const Engine::TextReader& r)
{
    comp.volumeId = r.U64("volumeId", comp.volumeId);
    comp.bEnabled = r.Bool("bEnabled", comp.bEnabled);
    comp.probeSpacing = r.Float("probeSpacing", comp.probeSpacing);
}

void LocalDDGIVolumeComponent::OnConstruct(entt::registry& registry, entt::entity entity)
{
    auto& comp = registry.get<LocalDDGIVolumeComponent>(entity);
    auto* state = registry.ctx().get<Engine::EngineState*>();
    if (comp.volumeId == 0) {
        comp.volumeId = state->rng();
    }
}
}
