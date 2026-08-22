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
#include "core/math/color_helpers.h"
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
        auto* state = registry.ctx().get<Engine::EngineState*>();
        DrawWindow(viewFamily, transform->translation, comp.probeSpacing, Core::Math::HashColor(comp.volumeId, 0u, 0.08f, 0.84f), state->projectConfig.reflectionProbeLineWidth);
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

Vec3 LocalDDGIVolumeComponent::WindowMin(const Vec3& corner, float spacing)
{
    const float s = glm::max(spacing, 0.25f);
    return glm::round(corner / s) * s;
}

void LocalDDGIVolumeComponent::DrawWindow(Core::ViewFamily& viewFamily, const Vec3& corner, float spacing, const Vec4& color, float lineWidth)
{
    const float s = glm::max(spacing, 0.25f);
    const Vec3 windowMin = WindowMin(corner, s);
    const Vec3 outerHalf = Vec3(static_cast<float>(Core::LOCAL_DDGI_PROBES_PER_AXIS - 1) * s * 0.5f);
    const Vec3 center = windowMin + outerHalf;
    const Vec3 innerHalf = outerHalf - Vec3(s);
    const Vec4 outerColor{color.r * 0.5f, color.g * 0.5f, color.b * 0.5f, color.a};
    constexpr Quat identity{1.0f, 0.0f, 0.0f, 0.0f};

    DEBUG_ADD_BOX(viewFamily.debugBoxes, {center, outerHalf, identity, outerColor, lineWidth});
    for (int i = 0; i < 3; ++i) {
        Vec3 u{0.0f};
        Vec3 v{0.0f};
        u[(i + 1) % 3] = outerHalf[(i + 1) % 3];
        v[(i + 2) % 3] = outerHalf[(i + 2) % 3];
        for (int side = 0; side < 2; ++side) {
            Vec3 faceCenter = center;
            faceCenter[i] += side == 0 ? outerHalf[i] : -outerHalf[i];
            DEBUG_ADD_LINE(viewFamily.debugLines, {faceCenter - u - v, faceCenter + u + v, outerColor, lineWidth});
            DEBUG_ADD_LINE(viewFamily.debugLines, {faceCenter - u + v, faceCenter + u - v, outerColor, lineWidth});
        }
    }
    DEBUG_ADD_BOX(viewFamily.debugBoxes, {center, innerHalf, identity, color, lineWidth});
}
}
