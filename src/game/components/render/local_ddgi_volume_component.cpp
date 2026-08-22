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
    static entt::entity editEntity = entt::null;
    static bool bEditing = false;

    if (editEntity != entity) {
        editEntity = entity;
        bEditing = false;
    }

    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto* transform = registry.try_get<TransformComponent>(entity);
    if (bEditing) {
        state->editor.bExclusiveGizmoActive = true;
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!popupOpen && state->input.GetActionState(Actions::ACTION_ESCAPE).down) {
            bEditing = false;
        }
    }

    bool open = ImGui::CollapsingHeader("Local DDGI Volume", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    bool remove = ImGui::SmallButton("X##deletelocalddgi");
    ImGui::PopStyleColor();

    bool modified = false;
    if (open) {
        auto& comp = registry.get<LocalDDGIVolumeComponent>(entity);

        modified |= ImGui::Checkbox("Enabled##lddgi", &comp.bEnabled);
        if (ImGui::DragFloat("Probe Spacing##lddgi", &comp.probeSpacing, 0.01f, 0.25f, 2.0f, "%.2f")) {
            comp.probeSpacing = glm::clamp(comp.probeSpacing, 0.25f, 2.0f);
            modified = true;
        }

        const float extent = static_cast<float>(Core::LOCAL_DDGI_PROBES_PER_AXIS - 1) * comp.probeSpacing;
        ImGui::Text("Window: %.2f m cube, owns %.2f m", extent, extent - 2.0f * comp.probeSpacing);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Translation = window centre; rotation and scale ignored. Face dots scale the cube about the centre. The box is the region this volume fully owns: fit interior faces inside it.");

        ImGui::PushStyleColor(ImGuiCol_Button, bEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditing);
        if (ImGui::Button(bEditing ? "Done##lddgi" : "Edit Window##lddgi")) {
            bEditing = !bEditing;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    }

    if (transform && bEditing) {
        auto& comp = registry.get<LocalDDGIVolumeComponent>(entity);
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        const auto& vd = viewFamily.mainView.currentViewData;
        const Vec4 viewport{
            static_cast<float>(ctx->windowContext.viewportOffsetX),
            static_cast<float>(ctx->windowContext.viewportOffsetY),
            static_cast<float>(ctx->windowContext.viewportWidth),
            static_cast<float>(ctx->windowContext.viewportHeight),
        };

        const float spacing = glm::max(comp.probeSpacing, 0.25f);
        const Vec3 center = transform->translation;
        const float halfExtent = static_cast<float>(Core::LOCAL_DDGI_PROBES_PER_AXIS - 1) * 0.5f * spacing;
        constexpr ImU32 handleColor = IM_COL32(110, 180, 255, 255);
        for (int i = 0; i < 3; ++i) {
            Vec3 axis{0.0f};
            axis[i] = 1.0f;
            for (int s = 0; s < 2; ++s) {
                const Vec3 outward = s == 0 ? axis : -axis;
                const Vec3 handlePos = center + outward * halfExtent;
                const int32_t handleId = Editor::DotHandleId::LOCAL_DDGI_BOUNDS_BASE + i * 2 + s;
                // Uniform cube with one size knob: every face scales about the pivot so the centre gizmo stays true.
                Editor::AxisDotHandle(handleId, handlePos, outward, vd.view, vd.proj, viewport, vd.cameraPos, state,
                                      [&](Vec3 newPt) {
                                          const float newExtent = 2.0f * glm::dot(newPt - center, outward);
                                          comp.probeSpacing = glm::clamp(newExtent / static_cast<float>(Core::LOCAL_DDGI_PROBES_PER_AXIS - 1), 0.25f, 2.0f);
                                          modified = true;
                                      },
                                      handleColor);
            }
        }
    }

    if (transform && (open || bEditing)) {
        const auto& comp = registry.get<LocalDDGIVolumeComponent>(entity);
        DrawWindow(viewFamily, transform->translation, comp.probeSpacing, Core::Math::HashColor(comp.volumeId, 0u, 0.08f, 0.84f), state->projectConfig.reflectionProbeLineWidth, true);
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

Vec3 LocalDDGIVolumeComponent::WindowCorner(const Vec3& centre, float spacing)
{
    return centre - Vec3(static_cast<float>(Core::LOCAL_DDGI_PROBES_PER_AXIS - 1) * 0.5f * glm::max(spacing, 0.25f));
}

void LocalDDGIVolumeComponent::DrawWindow(Core::ViewFamily& viewFamily, const Vec3& centre, float spacing, const Vec4& color, float lineWidth, bool bCrosses)
{
    const float s = glm::max(spacing, 0.25f);
    const Vec3 ownedHalf = Vec3((static_cast<float>(Core::LOCAL_DDGI_PROBES_PER_AXIS - 1) * 0.5f - 1.0f) * s);
    constexpr Quat identity{1.0f, 0.0f, 0.0f, 0.0f};

    DEBUG_ADD_BOX(viewFamily.debugBoxes, {centre, ownedHalf, identity, color, lineWidth});
    if (!bCrosses) {
        return;
    }
    for (int i = 0; i < 3; ++i) {
        Vec3 u{0.0f};
        Vec3 v{0.0f};
        u[(i + 1) % 3] = ownedHalf[(i + 1) % 3];
        v[(i + 2) % 3] = ownedHalf[(i + 2) % 3];
        for (int side = 0; side < 2; ++side) {
            Vec3 faceCenter = centre;
            faceCenter[i] += side == 0 ? ownedHalf[i] : -ownedHalf[i];
            DEBUG_ADD_LINE(viewFamily.debugLines, {faceCenter - u - v, faceCenter + u + v, color, lineWidth});
            DEBUG_ADD_LINE(viewFamily.debugLines, {faceCenter - u + v, faceCenter + u - v, color, lineWidth});
        }
    }
}
}
