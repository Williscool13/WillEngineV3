//
// Created by William on 2026-08-03.
//

#include "local_ddgi_volume_component.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include "game/component-registry/component_editor.h"
#include "game/component-registry/editor_gizmo_helpers.h"
#include "game/component-registry/json_helpers.h"
#include "game/components/core_components.h"
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

    auto* transform = registry.try_get<TransformComponent>(entity);

    if (open) {
        auto& comp = registry.get<LocalDDGIVolumeComponent>(entity);

        ImGui::Checkbox("Enabled##lddgi", &comp.bEnabled);
        if (ImGui::DragFloat("Probe Spacing##lddgi", &comp.probeSpacing, 0.01f, 0.25f, 2.0f, "%.2f")) {
            comp.probeSpacing = glm::clamp(comp.probeSpacing, 0.25f, 2.0f);
        }
        if (ImGui::DragInt3("Probe Count##lddgi", comp.probeCount, 0.1f, 2, Core::LOCAL_DDGI_MAX_PROBES_PER_AXIS)) {
            for (int i = 0; i < 3; ++i) {
                comp.probeCount[i] = glm::clamp(comp.probeCount[i], 2, Core::LOCAL_DDGI_MAX_PROBES_PER_AXIS);
            }
        }

        const Vec3 extent = Vec3(comp.probeCount[0] - 1, comp.probeCount[1] - 1, comp.probeCount[2] - 1) * comp.probeSpacing;
        ImGui::Text("Window: %.2f x %.2f x %.2f m (%d probes)", extent.x, extent.y, extent.z, comp.probeCount[0] * comp.probeCount[1] * comp.probeCount[2]);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Translation = min corner (snapped to spacing); rotation and scale ignored.");

        ImGui::PushStyleColor(ImGuiCol_Button, bEditing ? Editor::BUTTON_EDITING : Editor::BUTTON_IDLE);
        ImGui::BeginDisabled((state->editor.bExclusiveGizmoActive || state->editor.bExclusiveGizmoActivePrev) && !bEditing);
        if (ImGui::Button(bEditing ? "Done##lddgi" : "Edit Bounds##lddgi")) {
            bEditing = !bEditing;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    }

    if (transform && (open || bEditing)) {
        const auto& comp = registry.get<LocalDDGIVolumeComponent>(entity);
        const float spacing = comp.probeSpacing;
        const Vec3 windowMin = glm::round(transform->translation / spacing) * spacing;
        const Vec3 windowMax = windowMin + Vec3(comp.probeCount[0] - 1, comp.probeCount[1] - 1, comp.probeCount[2] - 1) * spacing;

        constexpr Vec4 windowColor{0.95f, 0.9f, 0.35f, 1.0f};
        constexpr float lineWidth = 0.02f;
        DEBUG_ADD_BOX(viewFamily.debugBoxes, {(windowMin + windowMax) * 0.5f, (windowMax - windowMin) * 0.5f, Quat{1.0f, 0.0f, 0.0f, 0.0f}, windowColor, lineWidth});
    }

    if (transform && bEditing) {
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        const auto& vd = viewFamily.mainView.currentViewData;
        auto& comp = registry.get<LocalDDGIVolumeComponent>(entity);
        const float spacing = comp.probeSpacing;
        const Vec3 windowMin = glm::round(transform->translation / spacing) * spacing;
        const Vec3 windowMax = windowMin + Vec3(comp.probeCount[0] - 1, comp.probeCount[1] - 1, comp.probeCount[2] - 1) * spacing;
        const Vec3 center = (windowMin + windowMax) * 0.5f;

        const Vec4 viewport{
            static_cast<float>(ctx->windowContext.viewportOffsetX),
            static_cast<float>(ctx->windowContext.viewportOffsetY),
            static_cast<float>(ctx->windowContext.viewportWidth),
            static_cast<float>(ctx->windowContext.viewportHeight),
        };

        const Vec3 axes[3] = {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)};
        const ImU32 colors[3] = {Editor::COLOR_AXIS_X, Editor::COLOR_AXIS_Y, Editor::COLOR_AXIS_Z};
        for (int i = 0; i < 3; ++i) {
            for (int s = 0; s < 2; ++s) {
                const float sign = s == 0 ? 1.0f : -1.0f;
                const Vec3 outward = axes[i] * sign;
                const int32_t handleId = Editor::DotHandleId::LOCAL_DDGI_BOUNDS_BASE + i * 2 + s;
                Vec3 handlePos = center;
                handlePos[i] = sign > 0.0f ? windowMax[i] : windowMin[i];
                const float fixedFace = sign > 0.0f ? windowMin[i] : windowMax[i];

                Editor::AxisDotHandle(handleId, handlePos, outward,
                                      vd.view, vd.proj, viewport, vd.cameraPos, state,
                                      [&](Vec3 newPt) {
                                          const float newExtent = (newPt[i] - fixedFace) * sign;
                                          const int32_t newCount = glm::clamp(static_cast<int32_t>(glm::round(newExtent / spacing)) + 1, 2, Core::LOCAL_DDGI_MAX_PROBES_PER_AXIS);
                                          comp.probeCount[i] = newCount;
                                          if (sign < 0.0f) {
                                              transform->translation[i] = fixedFace - static_cast<float>(newCount - 1) * spacing;
                                              registry.emplace_or_replace<DirtyTransformTag>(entity);
                                          }
                                      },
                                      colors[i]);
            }
        }
    }

    return {.requestRemoval = remove};
}

void LocalDDGIVolumeComponent::Serialize(const LocalDDGIVolumeComponent& comp, nlohmann::json& json)
{
    json["volumeId"] = comp.volumeId;
    json["bEnabled"] = comp.bEnabled;
    json["probeSpacing"] = comp.probeSpacing;
    json["probeCount"] = {comp.probeCount[0], comp.probeCount[1], comp.probeCount[2]};
}

void LocalDDGIVolumeComponent::Deserialize(LocalDDGIVolumeComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.volumeId = json.value("volumeId", uint64_t{0});
    comp.bEnabled = json.value("bEnabled", true);
    comp.probeSpacing = json.value("probeSpacing", 0.5f);
    if (const auto it = json.find("probeCount"); it != json.end() && it->is_array() && it->size() == 3) {
        for (int i = 0; i < 3; ++i) {
            comp.probeCount[i] = glm::clamp((*it)[i].get<int32_t>(), 2, Core::LOCAL_DDGI_MAX_PROBES_PER_AXIS);
        }
    }
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
