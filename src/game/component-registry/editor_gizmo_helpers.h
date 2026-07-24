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
/** Axis-colored dot handles and ImDrawList geometry. */
constexpr ImU32 COLOR_AXIS_X = IM_COL32(220, 60, 60, 255);
constexpr ImU32 COLOR_AXIS_Y = IM_COL32(60, 220, 60, 255);
constexpr ImU32 COLOR_AXIS_Z = IM_COL32(60, 100, 220, 255);

/** Axis-colored debug geometry (DEBUG_ADD_* macros). */
constexpr Vec4 DEBUG_AXIS_X{0.86f, 0.24f, 0.24f, 1.0f};
constexpr Vec4 DEBUG_AXIS_Y{0.24f, 0.86f, 0.24f, 1.0f};
constexpr Vec4 DEBUG_AXIS_Z{0.24f, 0.39f, 0.86f, 1.0f};


/** Edit-mode toggle button background colors. */
constexpr ImVec4 BUTTON_TRANSPAREN{0.0f, 0.0f, 0.0f, 0.0f};
constexpr ImVec4 BUTTON_EDITING{0.15f, 0.65f, 0.15f, 1.0f};
constexpr ImVec4 BUTTON_IDLE{0.15f, 0.35f, 0.65f, 1.0f};

/** DotHandle fill states and outline. */
constexpr ImU32 DOT_HANDLE_DEFAULT = IM_COL32(128, 200, 255, 255);
constexpr ImU32 DOT_HANDLE_HOVERED = IM_COL32(200, 235, 255, 255);
constexpr ImU32 DOT_HANDLE_ACTIVE = IM_COL32(255, 255, 255, 255);
constexpr ImU32 DOT_HANDLE_OUTLINE = IM_COL32(255, 255, 255, 160);

namespace GizmoId
{
    constexpr int32_t STATIC_MESH_TRANSFORM = 9900;
    constexpr int32_t PROCEDURAL_MESH_TRANSFORM = 9901;
    constexpr int32_t REFLECTION_PROBE_CAPTURE = 9902;
    constexpr int32_t PHYSICS_SHAPE_OFFSET = 9903;
    constexpr int32_t PRIMARY_TRANSFORM = 9904;
    constexpr int32_t MULTI_SELECT_TRANSFORM = 9905;

    constexpr int32_t SPLINE_POINT_BASE = 100000;
    constexpr int32_t PATH_MOVER_POINT_BASE = 200000;
}

namespace DotHandleId
{
    constexpr int32_t PHYSICS_SHAPE_BASE = 10000; // +0..+2 (box extents / capsule / sphere)
    constexpr int32_t LIGHT_AREA_BASE = 20000; // +0..+1 (width / height)
    constexpr int32_t REFLECTION_PROBE_BOUNDS_BASE = 21000; // +0 (sphere radius) or +0..+5 (box faces, i*2+s)
}

bool WorldToScreen(Vec3 worldPos, const Mat4& view, const Mat4& proj, Vec4 viewport, ImVec2& outScreen);

Vec3 ScreenToRay(ImVec2 screenPos, const Mat4& view, const Mat4& proj, Vec4 viewport);

/**
 * Draws a dot handle in world space; calls onMoved(hitPoint) while dragging.
 * Sets state->editor.bExclusiveGizmoActive when hovered or active to suppress viewport selection.
 * Drag plane normal for a single-axis constraint: normalize(cameraForward - dot(cameraForward, axis) * axis).
 * @param handleId Unique integer per simultaneously-visible handle.
 * @return true if this handle is hovered or actively dragged this frame (lets callers give it input priority).
 */
template<typename Fn>
bool DotHandle(int32_t handleId, Vec3 worldPos, Vec3 dragPlaneNormal,
               const Mat4& view, const Mat4& proj, Vec4 viewport, Vec3 cameraPos,
               Engine::EngineState* state, Fn onMoved,
               ImU32 color = DOT_HANDLE_DEFAULT, float screenRadius = 7.0f)
{
    ImVec2 screen;
    if (!WorldToScreen(worldPos, view, proj, viewport, screen)) { return false; }

    const bool anotherDotActive = state->editor.activeDotHandleId >= 0 && state->editor.activeDotHandleId != handleId;
    if (anotherDotActive) { return false; }

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

    if (hovered || active) { state->editor.bExclusiveGizmoActive = true; }

    ImU32 fill = active ? DOT_HANDLE_ACTIVE : hovered ? DOT_HANDLE_HOVERED : color;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddCircleFilled(screen, screenRadius, fill);
    dl->AddCircle(screen, screenRadius + 1.5f, DOT_HANDLE_OUTLINE, 0, 1.5f);

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        Vec3 rayDir = ScreenToRay(mouse, view, proj, viewport);
        float denom = glm::dot(rayDir, dragPlaneNormal);
        if (glm::abs(denom) > 1e-5f) {
            float t = glm::dot(worldPos - cameraPos, dragPlaneNormal) / denom;
            if (t > 0.0f) { onMoved(cameraPos + t * rayDir); }
        }
    }

    return hovered || active;
}

/**
 * Dot handle constrained to the world-space line through worldPos along axis; onMoved receives the closest point on that line to the mouse ray.
 * Well-conditioned at every view angle, unlike a drag-plane intersection, which degenerates when the camera looks down the axis.
 */
template<typename Fn>
bool AxisDotHandle(int32_t handleId, Vec3 worldPos, Vec3 axis,
                   const Mat4& view, const Mat4& proj, Vec4 viewport, Vec3 cameraPos,
                   Engine::EngineState* state, Fn onMoved,
                   ImU32 color = DOT_HANDLE_DEFAULT, float screenRadius = 7.0f)
{
    ImVec2 screen;
    if (!WorldToScreen(worldPos, view, proj, viewport, screen)) { return false; }

    const bool anotherDotActive = state->editor.activeDotHandleId >= 0 && state->editor.activeDotHandleId != handleId;
    if (anotherDotActive) { return false; }

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

    if (hovered || active) { state->editor.bExclusiveGizmoActive = true; }

    ImU32 fill = active ? DOT_HANDLE_ACTIVE : hovered ? DOT_HANDLE_HOVERED : color;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddCircleFilled(screen, screenRadius, fill);
    dl->AddCircle(screen, screenRadius + 1.5f, DOT_HANDLE_OUTLINE, 0, 1.5f);

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        Vec3 rayDir = ScreenToRay(mouse, view, proj, viewport);
        Vec3 w0 = worldPos - cameraPos;
        float b = glm::dot(axis, rayDir);
        float denom = 1.0f - b * b;
        if (denom > 1e-6f) {
            float t = (glm::dot(w0, rayDir) - b * glm::dot(w0, axis)) / denom;
            float s = t * b - glm::dot(w0, axis);
            onMoved(worldPos + axis * s);
        }
    }

    return hovered || active;
}
} // Editor

#endif //WILL_ENGINE_EDITOR_GIZMO_HELPERS_H
