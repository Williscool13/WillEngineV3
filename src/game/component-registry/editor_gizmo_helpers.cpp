//
// Created by William on 2026-05-23.
//

#include "editor_gizmo_helpers.h"

#include <glm/glm.hpp>

namespace Editor
{
bool WorldToScreen(Vec3 worldPos, const Mat4& view, const Mat4& proj, Vec4 viewport, ImVec2& outScreen)
{
    Vec4 clip = proj * view * Vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f) { return false; }
    Vec3 ndc = Vec3(clip) / clip.w;
    outScreen.x = (ndc.x + 1.0f) * 0.5f * viewport.z + viewport.x;
    outScreen.y = (1.0f - ndc.y) * 0.5f * viewport.w + viewport.y;
    return true;
}

Vec3 ScreenToRay(ImVec2 screenPos, const Mat4& view, const Mat4& proj, Vec4 viewport)
{
    float ndcX = (screenPos.x - viewport.x) / viewport.z * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenPos.y - viewport.y) / viewport.w * 2.0f;
    Vec4 clipRay = {ndcX, ndcY, -1.0f, 1.0f};
    Vec4 viewRay = glm::inverse(proj) * clipRay;
    viewRay = {viewRay.x, viewRay.y, -1.0f, 0.0f};
    return glm::normalize(Vec3(glm::inverse(view) * viewRay));
}
} // Editor
