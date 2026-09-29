//
// Created by William on 2026-05-23.
//

#include "editor_gizmo_helpers.h"

#include <glm/glm.hpp>

#include "engine/asset_manager.h"
#include "engine/components/render_components.h"
#include "engine/include/engine_context.h"
#include "engine/resources/model/static_model.h"

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

bool MeshPivotPresets(Engine::EngineContext* ctx, const entt::registry& registry, entt::entity entity, const Quat& renderRotation, Vec3& renderOffset)
{
    Vec3 boundsMin{1.0f};
    Vec3 boundsMax{-1.0f};
    if (const auto* rt = registry.try_get<Engine::Component::MeshRuntime>(entity); rt && rt->modelHandle.IsValid()) {
        const Engine::StaticModel* model = ctx->assetManager->GetModel(rt->modelHandle);
        if (model && model->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded) {
            boundsMin = model->bounds.aabb.min;
            boundsMax = model->bounds.aabb.max;
        }
    }

    struct Preset
    {
        const char* label;
        Vec3 point;
    };
    const Vec3 c = (boundsMin + boundsMax) * 0.5f;
    const Preset presets[] = {
        {"Center", c},
        {"Bottom", {c.x, boundsMin.y, c.z}},
        {"Top", {c.x, boundsMax.y, c.z}},
        {"Corner", boundsMin},
    };

    bool bPicked = false;
    ImGui::PushID("pivot_presets");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Pivot");
    ImGui::BeginDisabled(boundsMin.x > boundsMax.x);
    for (const Preset& preset : presets) {
        ImGui::SameLine();
        if (ImGui::SmallButton(preset.label)) {
            renderOffset = -(renderRotation * preset.point);
            bPicked = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Origin")) {
        renderOffset = Vec3(0.0f);
        bPicked = true;
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("The mesh's own origin (no offset)"); }
    ImGui::PopID();
    return bPicked;
}
} // Editor
