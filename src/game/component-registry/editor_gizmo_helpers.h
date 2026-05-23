//
// Created by William on 2026-05-23.
//

#ifndef WILL_ENGINE_EDITOR_GIZMO_HELPERS_H
#define WILL_ENGINE_EDITOR_GIZMO_HELPERS_H

#include <imgui.h>
#include <ImGuizmo.h>
#include "core/types/math.h"
#include "engine/engine_api.h"

namespace Editor
{
// --- Centralized editor colors ---

/** Axis-colored dot handles and ImDrawList geometry. */
constexpr ImU32 ColorAxisX = IM_COL32(220, 60,  60,  255);
constexpr ImU32 ColorAxisY = IM_COL32(60,  220, 60,  255);
constexpr ImU32 ColorAxisZ = IM_COL32(60,  100, 220, 255);

/** Axis-colored debug geometry (DEBUG_ADD_* macros). */
constexpr Vec4 DebugAxisX{0.86f, 0.24f, 0.24f, 1.0f};
constexpr Vec4 DebugAxisY{0.24f, 0.86f, 0.24f, 1.0f};
constexpr Vec4 DebugAxisZ{0.24f, 0.39f, 0.86f, 1.0f};


/** Edit-mode toggle button background colors. */
constexpr ImVec4 ButtonTransparent{0.0f, 0.0f, 0.0f, 0.0f};
constexpr ImVec4 ButtonEditing{0.15f, 0.65f, 0.15f, 1.0f};
constexpr ImVec4 ButtonIdle{0.15f, 0.35f, 0.65f, 1.0f};

// --- Helpers ---

bool WorldToScreen(Vec3 worldPos, const Mat4& view, const Mat4& proj, Vec4 viewport, ImVec2& outScreen);
Vec3 ScreenToRay(ImVec2 screenPos, const Mat4& view, const Mat4& proj, Vec4 viewport);

/**
 * Draws a dot handle in world space; calls onMoved(hitPoint) while dragging.
 * Sets state->editor.bCustomGizmoActive when hovered or active to suppress viewport selection.
 * Drag plane normal for a single-axis constraint: normalize(cameraForward - dot(cameraForward, axis) * axis).
 * @param handleId Unique integer per simultaneously-visible handle.
 */
template<typename Fn>
void DotHandle(int32_t handleId, Vec3 worldPos, Vec3 dragPlaneNormal,
               const Mat4& view, const Mat4& proj, Vec4 viewport, Vec3 cameraPos,
               Engine::EngineState* state, Fn onMoved,
               ImU32 color = IM_COL32(128, 200, 255, 255), float screenRadius = 7.0f)
{
    ImVec2 screen;
    if (!WorldToScreen(worldPos, view, proj, viewport, screen)) { return; }

    ImVec2 mouse = ImGui::GetIO().MousePos;
    float dx = mouse.x - screen.x;
    float dy = mouse.y - screen.y;
    bool hovered = (dx * dx + dy * dy <= screenRadius * screenRadius) && !ImGuizmo::IsUsing();
    bool active = (state->editor.activeDotHandleId == handleId);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsUsing()) {
        state->editor.activeDotHandleId = handleId;
        active = true;
    }
    if (active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        state->editor.activeDotHandleId = -1;
        active = false;
    }

    if (hovered || active) { state->editor.bCustomGizmoActive = true; }

    ImU32 fill = active  ? IM_COL32(255, 255, 255, 255)
               : hovered ? IM_COL32(200, 235, 255, 255)
               : color;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddCircleFilled(screen, screenRadius, fill);
    dl->AddCircle(screen, screenRadius + 1.5f, IM_COL32(255, 255, 255, 160), 0, 1.5f);

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        Vec3 rayDir = ScreenToRay(mouse, view, proj, viewport);
        float denom = glm::dot(rayDir, dragPlaneNormal);
        if (glm::abs(denom) > 1e-5f) {
            float t = glm::dot(worldPos - cameraPos, dragPlaneNormal) / denom;
            if (t > 0.0f) { onMoved(cameraPos + t * rayDir); }
        }
    }
}
} // Editor

#endif //WILL_ENGINE_EDITOR_GIZMO_HELPERS_H
