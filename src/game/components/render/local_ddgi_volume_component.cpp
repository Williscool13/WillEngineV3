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

        if (transform) {
            const Vec3 aabbMin = transform->translation - transform->scale;
            const Vec3 aabbMax = transform->translation + transform->scale;
            const float spacing = comp.probeSpacing;
            const glm::ivec3 baseCell = glm::ivec3(glm::floor(aabbMin / spacing));
            const glm::ivec3 rawCount = glm::ivec3(glm::ceil(aabbMax / spacing)) - baseCell + 1;
            const glm::ivec3 count = glm::clamp(rawCount, 2, Core::LOCAL_DDGI_MAX_PROBES_PER_AXIS);
            ImGui::Text("Probes: %d x %d x %d (%d)", count.x, count.y, count.z, count.x * count.y * count.z);
            if (rawCount != count) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Window clamped to %d probes/axis. Shrink bounds or raise spacing.", Core::LOCAL_DDGI_MAX_PROBES_PER_AXIS);
            }
            if (glm::abs(glm::dot(transform->rotation, Quat{1.0f, 0.0f, 0.0f, 0.0f})) < 0.9999f) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Rotation ignored; the probe window is world-axis aligned.");
            }
        }

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
        const glm::ivec3 baseCell = glm::ivec3(glm::floor((transform->translation - transform->scale) / spacing));
        const glm::ivec3 count = glm::clamp(glm::ivec3(glm::ceil((transform->translation + transform->scale) / spacing)) - baseCell + 1, 2, Core::LOCAL_DDGI_MAX_PROBES_PER_AXIS);

        constexpr Vec4 boundsColor{0.35f, 0.8f, 0.95f, 1.0f};
        constexpr Vec4 windowColor{0.95f, 0.9f, 0.35f, 1.0f};
        constexpr float lineWidth = 0.02f;
        DEBUG_ADD_BOX(viewFamily.debugBoxes, {transform->translation, transform->scale, Quat{1.0f, 0.0f, 0.0f, 0.0f}, boundsColor, lineWidth});
        const Vec3 windowMin = Vec3(baseCell) * spacing;
        const Vec3 windowMax = Vec3(baseCell + count - 1) * spacing;
        DEBUG_ADD_BOX(viewFamily.debugBoxes, {(windowMin + windowMax) * 0.5f, (windowMax - windowMin) * 0.5f, Quat{1.0f, 0.0f, 0.0f, 0.0f}, windowColor, lineWidth});
    }

    if (transform && bEditing) {
        auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        const auto& vd = viewFamily.mainView.currentViewData;
        const Vec3 center = transform->translation;

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
                const Vec3 handlePos = center + outward * transform->scale[i];

                Editor::AxisDotHandle(handleId, handlePos, outward,
                                      vd.view, vd.proj, viewport, vd.cameraPos, state,
                                      [&](Vec3 newPt) {
                                          const Vec3 opposite = center - outward * transform->scale[i];
                                          const float newExtentFull = glm::dot(newPt - opposite, outward);
                                          const float newHalf = glm::max(0.05f, newExtentFull * 0.5f);
                                          transform->scale[i] = newHalf;
                                          transform->translation = opposite + outward * newHalf;
                                          registry.emplace_or_replace<DirtyTransformTag>(entity);
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
}

void LocalDDGIVolumeComponent::Deserialize(LocalDDGIVolumeComponent& comp, const nlohmann::json& json)
{
    if (!json.is_object()) { return; }
    comp.volumeId = json.value("volumeId", uint64_t{0});
    comp.bEnabled = json.value("bEnabled", true);
    comp.probeSpacing = json.value("probeSpacing", 0.5f);
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
