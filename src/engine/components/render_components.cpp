//
// Created by William on 2026-01-30.
//

#include "render_components.h"
#include "render/static_mesh_component.h"

#include <entt/entt.hpp>

#include "imgui.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/systems/render_systems.h"
#include "engine/editor/edit_widgets.h"

namespace Engine::Component
{
void RenderFlagsComponent::OnEditPreview(entt::registry& registry, entt::entity entity)
{
    EvaluateInstanceRenderState(registry.ctx().get<Engine::EngineState*>(), entity);
}

void RenderFlagsComponent::OnEditCommit(entt::registry& registry, entt::entity entity)
{
    EvaluateInstanceRenderState(registry.ctx().get<Engine::EngineState*>(), entity);
}

bool DrawRenderFlagToggles(Engine::EditContext& edit, uint32_t toggles)
{
    ImGui::PushID("renderflags");
    if (toggles & RENDER_TOGGLE_VISIBLE) {
        EditWidgets::Checkbox(edit, "Visible", &RenderFlagsComponent::bVisible);
        ImGui::SameLine();
    }
    if (toggles & RENDER_TOGGLE_DDGI) {
        EditWidgets::Checkbox(edit, "DDGI Contribution", &RenderFlagsComponent::bDdgiContribute);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (toggles & RENDER_TOGGLE_PROBE_BAKE) {
        EditWidgets::Checkbox(edit, "Bake", &RenderFlagsComponent::bBakeInclude);
        ImGui::SameLine();
    }
    if (toggles & RENDER_TOGGLE_MOTION_BLUR) {
        EditWidgets::Checkbox(edit, "Motion Blur", &RenderFlagsComponent::bMotionBlur);
        ImGui::SameLine();
    }
    if (toggles & RENDER_TOGGLE_CAMERA_MOTION_BLUR) {
        EditWidgets::Checkbox(edit, "Camera Motion Blur", &RenderFlagsComponent::bCameraMotionBlur);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (toggles & RENDER_TOGGLE_ALPHA_CUTOUT) {
        EditWidgets::Checkbox(edit, "Alpha Cutout", &RenderFlagsComponent::bAlphaCutout);
        ImGui::SameLine();
    }
    bool bEmissiveChanged = false;
    if (toggles & RENDER_TOGGLE_EMISSIVE) {
        bEmissiveChanged = EditWidgets::Checkbox(edit, "Emissive Light", &RenderFlagsComponent::bEmissiveLight);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::PopID();
    return bEmissiveChanged;
}

void MeshRuntime::OnConstruct(entt::registry& registry, entt::entity entity)
{
    if (const auto* stable = registry.try_get<StableIdComponent>(entity)) {
        registry.get<MeshRuntime>(entity).stableId = stable->id.id;
    }
}

void MeshRuntime::OnDestroy(entt::registry& registry, entt::entity entity)
{
    auto* state = registry.ctx().get<Engine::EngineState*>();
    auto& runtime = registry.get<MeshRuntime>(entity);

    state->commandQueue.Push({.type = CommandType::MeshRelease, .payload = {.meshRelease = {runtime.range.offset, runtime.range.count, runtime.modelRange.offset, runtime.modelRange.count, runtime.modelHandle, runtime.pendingModelHandle}}});
    runtime.range = {};
    runtime.modelRange = {};
    runtime.modelHandle = StaticModelHandle::INVALID;
    runtime.pendingModelHandle = StaticModelHandle::INVALID;
}
}
