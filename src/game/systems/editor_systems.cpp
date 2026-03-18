//
// Created by William on 2026-01-30.
//

#include "editor_systems.h"

#include <algorithm>

#include <tracy/Tracy.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "debug_system.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "scene_system.h"
#include "core/include/engine_context.h"
#include "core/input/input_frame.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "engine/resources/material/material_manager.h"
#include "engine/asset_manager.h"
#include "engine/core/model_id.h"
#include "engine/resources/texture/texture.h"
#include "game/fwd_components.h"
#include "game/components/common_components.h"
#include "game/components/scene_components.h"

namespace Game
{
static void MarkSceneModified(Engine::GameState* state, StringID sceneId)
{
    if (std::ranges::find(state->modifiedScenes, sceneId) == state->modifiedScenes.end()) {
        state->modifiedScenes.push_back(sceneId);
    }
}

void EditorUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (state->bAutoSave && !state->modifiedScenes.empty()) {
        state->autoSaveTimer += state->timeFrame->deltaTime;
        if (state->autoSaveTimer >= state->autoSaveInterval) {
            state->autoSaveTimer = 0.0f;
            for (StringID sceneId : state->modifiedScenes) {
                const auto& sceneCache = ctx->assetManager->GetSceneCache();
                if (auto it = sceneCache.find(sceneId); it != sceneCache.end()) {
                    SaveSceneToFile(sceneId, it->second.sceneName, state, ctx->assetManager, ctx);
                }
            }
            state->modifiedScenes.clear();
        }
    }

    bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (state->bIsPlaying) {
        if (!popupOpen && state->inputFrame->GetKey(Key::ESCAPE).pressed) {
            PlayStop(ctx, state);
        }
        return;
    }

    if (ctx->bImGuiWantsTextInput) { return; }

    const bool ctrlHeld = state->inputFrame->GetKey(Key::LCTRL).down || state->inputFrame->GetKey(Key::RCTRL).down;

    const bool multiSelectActive = state->selectedEntities.size() > 1;
    if (multiSelectActive && state->currentGizmoOperation == ImGuizmo::SCALE) {
        state->currentGizmoOperation = ImGuizmo::TRANSLATE;
    }
    if (!ctrlHeld) {
        if (state->inputFrame->GetKey(Key::W).pressed) {
            state->currentGizmoOperation = ImGuizmo::TRANSLATE;
        }
        else if (state->inputFrame->GetKey(Key::E).pressed) {
            state->currentGizmoOperation = ImGuizmo::ROTATE;
        }
        else if (state->inputFrame->GetKey(Key::R).pressed && !multiSelectActive) {
            state->currentGizmoOperation = ImGuizmo::SCALE;
        }
    }

    const bool rmbHeld = state->inputFrame->GetMouse(MouseButton::RMB).down;
    if (!rmbHeld) {
        if (!popupOpen && ctrlHeld && state->inputFrame->GetKey(Key::W).pressed) {
            state->bWantCopyEntities = true;
        }

        if (!popupOpen && state->inputFrame->GetKey(Key::DEL).pressed) {
            state->bWantDeleteEntities = true;
        }

        if (!popupOpen && state->inputFrame->GetKey(Key::ESCAPE).pressed) {
            state->selectedEntities.clear();
        }

        if (!popupOpen && state->inputFrame->GetKey(Key::F).pressed && !state->selectedEntities.empty()) {
            entt::entity target = state->selectedEntities.front();
            if (state->registry.valid(target)) {
                auto* targetTransform = state->registry.try_get<Component::TransformComponent>(target);

                auto editorCamView = state->registry.view<Component::EditorCameraTag, Component::FreeCameraComponent, Component::TransformComponent>();
                for (entt::entity camEntity : editorCamView) {
                    auto& camTransform = editorCamView.get<Component::TransformComponent>(camEntity);
                    const glm::vec3 camForward = glm::normalize(camTransform.rotation * WORLD_FORWARD);

                    glm::vec3 focusPoint = targetTransform ? targetTransform->translation : glm::vec3(0.0f);
                    float focusDist = 1.0f;

                    const auto* staticMesh = state->registry.try_get<Component::StaticMeshComponent>(target);
                    const auto* proceduralMesh = state->registry.try_get<Component::ProceduralMeshComponent>(target);

                    Engine::StaticModelHandle modelHandle = Engine::StaticModelHandle::INVALID;
                    if (staticMesh && staticMesh->modelHandle.IsValid()) {
                        modelHandle = staticMesh->modelHandle;
                    } else if (proceduralMesh && proceduralMesh->modelHandle.IsValid()) {
                        modelHandle = proceduralMesh->modelHandle;
                    }

                    if (modelHandle.IsValid()) {
                        if (const Engine::StaticModel* model = ctx->assetManager->GetModel(modelHandle)) {
                            if (model->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded) {
                                const Engine::AABB& aabb = model->bounds.aabb;
                                const glm::vec3 localCenter = aabb.Center();
                                const glm::vec3 localHalf = aabb.HalfExtents();

                                if (targetTransform) {
                                    const glm::mat4 worldMatrix = GetMatrix(*targetTransform);
                                    focusPoint = glm::vec3(worldMatrix * glm::vec4(localCenter, 1.0f));
                                    const glm::vec3 scale = targetTransform->scale;
                                    const float maxScale = glm::max(glm::max(glm::abs(scale.x), glm::abs(scale.y)), glm::abs(scale.z));
                                    focusDist = glm::length(localHalf) * maxScale + 2.0f;
                                }
                            }
                        }
                    }

                    camTransform.translation = focusPoint - camForward * focusDist;
                    break;
                }
            }
        }
    }


    if (!ctx->bImguiMouseCaptured && state->inputFrame->GetMouse(MouseButton::LMB).pressed) {
        auto it = state->stableIdToEntityMap.find(StringID{ctx->lastKnownStableIdUnderCursor});
        if (it != state->stableIdToEntityMap.end()) {
            entt::entity clicked = it->second;
            if (ctrlHeld) {
                auto pos = std::find(state->selectedEntities.begin(), state->selectedEntities.end(), clicked);
                if (pos != state->selectedEntities.end()) {
                    state->selectedEntities.erase(pos);
                }
                else {
                    state->selectedEntities.push_back(clicked);
                }
            }
            else {
                state->selectedEntities = {clicked};
            }
        }
        else if (!ctrlHeld) {
            state->selectedEntities.clear();
        }
    }

    for (const auto& hotkey : DEBUG_HOTKEYS) {
        if (state->inputFrame->GetKey(hotkey.key).pressed) {
            if (state->debugResourceName == hotkey.resourceName && state->debugViewAspect == hotkey.aspect) {
                state->debugResourceName.clear();
            }
            else {
                state->debugResourceName = hotkey.resourceName;
                state->debugTransformationType = hotkey.transform;
                state->debugViewAspect = hotkey.aspect;
            }
        }
    }
}

void DrawEditorInterface(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    state->texResidency.Tick(ctx);

    if (state->bWantDeleteEntities) {
        state->bWantDeleteEntities = false;
        for (entt::entity entity : state->selectedEntities) {
            if (!state->registry.valid(entity)) continue;
            for (auto& entry : state->componentRegistry.registry) {
                if (entry.has(state->registry, entity)) {
                    entry.onRemoveComponent(state->registry, entity);
                }
            }
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                state->stableIdToEntityMap.erase(stable->id);
            }
            state->registry.destroy(entity);
        }
        state->selectedEntities.clear();
    }

    if (state->bWantCopyEntities) {
        state->bWantCopyEntities = false;
        std::vector<entt::entity> copies;
        copies.reserve(state->selectedEntities.size());
        for (entt::entity entity : state->selectedEntities) {
            if (!state->registry.valid(entity)) continue;
            copies.push_back(CopySceneEntity(state, entity, state->currentSceneId));
        }
        state->selectedEntities = copies;
    }

    if (ImGui::Begin("Debug View")) {
        /*auto cameraView = state->registry.view<Component::CameraComponent, Component::GameCameraTag, Component::TransformComponent>();
        const auto& [cam, transform] = cameraView.get(cameraView.front());
        ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)",
                    transform.translation.x, transform.translation.y, transform.translation.z);
        ImGui::Text("Camera Forward: (%.2f, %.2f, %.2f)",
                    cam.currentViewData.cameraForward.x,
                    cam.currentViewData.cameraForward.y,
                    cam.currentViewData.cameraForward.z);*/

        ImGui::Text("Current Debug View: %s", state->debugResourceName.empty() ? "None" : state->debugResourceName.c_str());
        ImGui::Checkbox("Enable Portals", &state->bEnablePortal);

        if (ImGui::Button("Disable Debug View")) {
            state->debugResourceName.clear();
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Hotkeys", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* keyNames[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
            for (size_t i = 0; i < std::size(DEBUG_HOTKEYS); ++i) {
                ImGui::Text("%s: %s (%s)", keyNames[i], DEBUG_HOTKEYS[i].name, DEBUG_HOTKEYS[i].resourceName);
            }
        }

        ImGui::Separator();

        auto setDebugTarget = [&](const char* name, DebugTransformationType _transform, Core::DebugViewAspect aspect) {
            if (state->debugResourceName == name && state->debugViewAspect == aspect) {
                state->debugResourceName.clear();
            }
            else {
                state->debugResourceName = name;
                state->debugTransformationType = _transform;
                state->debugViewAspect = aspect;
            }
        };
        if (ImGui::CollapsingHeader("G-Buffer")) {
            if (ImGui::Button("Depth Target")) setDebugTarget("depth_target", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Stencil Target")) setDebugTarget("depth_target", DebugTransformationType::StencilRemap, Core::DebugViewAspect::Stencil);
            if (ImGui::Button("Albedo Target")) setDebugTarget("albedo_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Normal Target")) setDebugTarget("normal_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("PBR Target")) setDebugTarget("pbr_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Emissive Target")) setDebugTarget("emissive_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Velocity Target")) setDebugTarget("velocity_target", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Shadows")) {
            if (ImGui::Button("Shadow Cascade 0")) setDebugTarget("shadow_cascade_0", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadow Cascade 1")) setDebugTarget("shadow_cascade_1", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadow Cascade 2")) setDebugTarget("shadow_cascade_2", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadow Cascade 3")) setDebugTarget("shadow_cascade_3", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadows Resolve")) setDebugTarget("shadows_resolve_target", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Lighting")) {
            if (ImGui::Button("Deferred Resolve")) setDebugTarget("deferred_resolve_target", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Depth")) setDebugTarget("gtao_depth", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO AO")) setDebugTarget("gtao_ao", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Edges")) setDebugTarget("gtao_edges", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Filtered")) setDebugTarget("gtao_filtered", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Anti-Aliasing")) {
            if (ImGui::Button("TAA Current")) setDebugTarget("taa_current", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("TAA Output")) setDebugTarget("taa_output", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Portal")) {
            if (ImGui::Button("Portal Albedo")) setDebugTarget("portal_albedo", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal Normal")) setDebugTarget("portal_normal", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal PBR")) setDebugTarget("portal_pbr", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal Emissive")) setDebugTarget("portal_emissive", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal Velocity")) setDebugTarget("portal_velocity", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Portal Depth")) setDebugTarget("portal_depth", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Portal Deferred Resolve")) setDebugTarget("portal_deferred_resolve", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Post-Processing")) {
            if (ImGui::Button("Bloom Chain")) setDebugTarget("bloom_chain", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Sharpening Output")) setDebugTarget("sharpening_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Tonemap Output")) setDebugTarget("tonemap_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Tiled Max")) setDebugTarget("motion_blur_tiled_max", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Neighbor Max")) setDebugTarget("motion_blur_tiled_neighbor_max", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Motion Blur Output")) setDebugTarget("motion_blur_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Color Grading Output")) setDebugTarget("color_grading_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Vignette Aberration Output")) setDebugTarget("vignette_aberration_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Post Process Output")) setDebugTarget("post_process_output", DebugTransformationType::None, Core::DebugViewAspect::None);
        }
    }
    ImGui::End();

    auto editorCameraView = state->registry.view<Component::FreeCameraComponent, Component::TransformComponent, Component::EditorCameraTag>();
    assert(editorCameraView.size_hint() > 0);
    auto [freeCam, editorCameraTransform] = editorCameraView.get<Component::FreeCameraComponent, Component::TransformComponent>(editorCameraView.front());

    ImGuizmo::SetOrthographic(freeCam.bOrtho);
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(
        static_cast<float>(ctx->windowContext.viewportOffsetX),
        static_cast<float>(ctx->windowContext.viewportOffsetY),
        static_cast<float>(ctx->windowContext.viewportWidth),
        static_cast<float>(ctx->windowContext.viewportHeight)
    );

    // View Manipulator Gizmo
    {
        const float gizmoSize = 128.0f;
        const auto vpRight = static_cast<float>(ctx->windowContext.viewportOffsetX + ctx->windowContext.viewportWidth);
        const auto vpTop = static_cast<float>(ctx->windowContext.viewportOffsetY);

        // Determine orbit pivot: selected object's world pos, or 8 units ahead of camera
        glm::vec3 orbitPivot = editorCameraTransform.translation + frameBuffer->mainViewFamily.mainView.currentViewData.cameraForward * 8.0f;
        float orbitDist = 8.0f;
        bool orbitAroundObject = false;

        if (!state->selectedEntities.empty()) {
            entt::entity target = state->selectedEntities.front();
            if (state->registry.valid(target)) {
                if (const auto* targetTransform = state->registry.try_get<Component::TransformComponent>(target)) {
                    orbitPivot = targetTransform->translation;
                    orbitDist = glm::max(glm::length(orbitPivot - editorCameraTransform.translation), 0.1f);
                    orbitAroundObject = true;
                }
            }
        }

        glm::mat4 viewCopy = frameBuffer->mainViewFamily.mainView.currentViewData.view;
        ImGuizmo::ViewManipulate(glm::value_ptr(viewCopy), orbitDist, ImVec2(vpRight - gizmoSize, vpTop), ImVec2(gizmoSize, gizmoSize), 0x10101080);

        if (viewCopy != frameBuffer->mainViewFamily.mainView.currentViewData.view) {
            const glm::mat4 invView = glm::inverse(viewCopy);
            const glm::vec3 newForward = -glm::normalize(glm::vec3(invView[2]));
            const glm::vec3 newUp = glm::normalize(glm::vec3(invView[1]));

            editorCameraTransform.rotation = glm::normalize(glm::quat_cast(glm::mat3(invView)));

            if (orbitAroundObject) {
                editorCameraTransform.translation = orbitPivot - newForward * orbitDist;
            }

            frameBuffer->mainViewFamily.mainView.currentViewData.cameraForward = newForward;
            frameBuffer->mainViewFamily.mainView.currentViewData.cameraLookAt = editorCameraTransform.translation + newForward;
            frameBuffer->mainViewFamily.mainView.currentViewData.view = glm::lookAt(editorCameraTransform.translation, editorCameraTransform.translation + newForward, newUp);
        }

        constexpr ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                  ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::SetNextWindowPos(ImVec2(vpRight - gizmoSize, vpTop + gizmoSize), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(gizmoSize, 0), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.5f);
        if (ImGui::Begin("##cam_overlay", nullptr, overlayFlags)) {
            if (ImGui::Button(freeCam.bOrtho ? "Ortho" : "Persp", ImVec2(-1, 0))) {
                freeCam.bOrtho = !freeCam.bOrtho;
            }
        }
        ImGui::End();
    }

    const glm::mat4 view = frameBuffer->mainViewFamily.mainView.currentViewData.view;
    const glm::mat4 proj = frameBuffer->mainViewFamily.mainView.currentViewData.proj;
    const glm::vec3 cameraPos = frameBuffer->mainViewFamily.mainView.currentViewData.cameraPos;
    const glm::vec3 cameraFwd = frameBuffer->mainViewFamily.mainView.currentViewData.cameraForward;

    const bool multiSelected = state->selectedEntities.size() > 1;
    if (multiSelected) {
        state->currentGizmoMode = ImGuizmo::WORLD;
    }

    if (ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (ImGui::RadioButton("T##gizmo_op", state->currentGizmoOperation == ImGuizmo::TRANSLATE)) { state->currentGizmoOperation = ImGuizmo::TRANSLATE; }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Translate (W)"); }
        ImGui::SameLine();
        if (ImGui::RadioButton("R##gizmo_op", state->currentGizmoOperation == ImGuizmo::ROTATE)) { state->currentGizmoOperation = ImGuizmo::ROTATE; }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Rotate (E)"); }
        ImGui::SameLine();
        ImGui::BeginDisabled(multiSelected);
        if (ImGui::RadioButton("S##gizmo_op", state->currentGizmoOperation == ImGuizmo::SCALE)) { state->currentGizmoOperation = ImGuizmo::SCALE; }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(multiSelected ? "Scale (R) — unavailable for multi-selection" : "Scale (R)");
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        ImGui::BeginDisabled(state->currentGizmoOperation == ImGuizmo::SCALE || multiSelected);
        if (ImGui::RadioButton("L##gizmo_mode", state->currentGizmoMode == ImGuizmo::LOCAL)) { state->currentGizmoMode = ImGuizmo::LOCAL; }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("Local space"); }
        ImGui::SameLine();
        if (ImGui::RadioButton("W##gizmo_mode", state->currentGizmoMode == ImGuizmo::WORLD)) { state->currentGizmoMode = ImGuizmo::WORLD; }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("World space"); }
        ImGui::EndDisabled();

        if (state->currentGizmoOperation == ImGuizmo::SCALE) {
            ImGui::SameLine();
            ImGui::Checkbox("Uni##gizmo_uni", &state->bUniformScaleMode);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Uniform scale"); }
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        ImGui::Checkbox("Snap##gizmo_snap", &state->bSnapEnabled);
        if (state->bSnapEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55.0f);
            if (state->currentGizmoOperation == ImGuizmo::TRANSLATE) {
                ImGui::DragFloat("##snap_val", &state->snapTranslation, 0.05f, 0.01f, 10.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translation snap (world units)");
            }
            else if (state->currentGizmoOperation == ImGuizmo::ROTATE) {
                ImGui::DragFloat("##snap_val", &state->snapRotation, 1.0f, 1.0f, 180.0f, "%.0f deg");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotation snap (degrees)");
            }
            else {
                ImGui::DragFloat("##snap_val", &state->snapScale, 0.05f, 0.01f, 2.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale snap");
            }
            if (state->currentGizmoOperation == ImGuizmo::TRANSLATE) {
                ImGui::SameLine();
                ImGui::Checkbox("World Grid##snap_world_grid", &state->bSnapWorldGrid);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap to world-origin-aligned grid rather than drag-relative increments");
            }
        }

        if (state->prevSelectedEntities != state->selectedEntities) {
            if (state->selectedEntities.size() == 1) {
                if (auto* tf = state->registry.try_get<Component::TransformComponent>(state->selectedEntities[0])) {
                    const bool scaleIsUniform = glm::epsilonEqual(tf->scale.x, tf->scale.y, 1e-5f) && glm::epsilonEqual(tf->scale.y, tf->scale.z, 1e-5f);
                    state->bUniformScaleMode = scaleIsUniform;
                }
            }
        }
        state->prevSelectedEntities = state->selectedEntities;

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (state->bIsPlaying) {
            if (ImGui::Button("Stop")) {
                PlayStop(ctx, state);
            }
        }
        else {
            if (ImGui::Button("Play")) {
                PlayStart(ctx, state);
            }
        }

        // Right-aligned physics debug dropdown
        {
            static constexpr const char* kPhysicsDebugLabels[] = {"Off", "Tag Only", "On"};
            int currentMode = static_cast<int>(state->physicsDebugMode);
            const float comboW = 90.0f;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - comboW);
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::Combo("##physics_debug", &currentMode, kPhysicsDebugLabels, IM_ARRAYSIZE(kPhysicsDebugLabels)))
                state->physicsDebugMode = static_cast<Engine::GameState::PhysicsDebugMode>(currentMode);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Physics debug draw: Off / Tag Only / On (all)");
        }
    }
    ImGui::End();

    if (ImGui::Begin("Scene Browser")) {
        const auto& sceneCache = ctx->assetManager->GetSceneCache();
        static int selectedScene = 0;
        std::vector<std::pair<std::string, StringID> > sceneList;
        sceneList.reserve(sceneCache.size());
        for (const auto& [id, meta] : sceneCache) {
            sceneList.emplace_back(meta.sceneName, id);
        }
        if (!sceneList.empty()) {
            std::ranges::sort(sceneList, {}, &std::pair<std::string, StringID>::first);
            selectedScene = std::clamp(selectedScene, 0, static_cast<int>(sceneList.size()) - 1);
        }

        const char* previewLabel = sceneList.empty() ? "" : sceneList[selectedScene].first.c_str();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##scene_list", previewLabel)) {
            for (int i = 0; i < static_cast<int>(sceneList.size()); ++i) {
                const bool loaded = std::ranges::find(state->loadedScenes, sceneList[i].second) != state->loadedScenes.end();
                if (ImGui::Selectable(sceneList[i].first.c_str(), i == selectedScene)) {
                    selectedScene = i;
                    state->currentSceneId = sceneList[i].second;
                    state->currentSceneName = sceneList[i].first;
                }
                if (loaded) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(loaded)");
                }
            }
            ImGui::EndCombo();
        }

        const bool selectedIsLoaded = !sceneList.empty() && std::ranges::find(state->loadedScenes, sceneList[selectedScene].second) != state->loadedScenes.end();

        ImGui::BeginDisabled(sceneList.empty() || selectedIsLoaded);
        if (ImGui::Button("Load")) { LoadSceneFromFile(state, ctx->assetManager, sceneList[selectedScene].second); }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!selectedIsLoaded);
        if (ImGui::Button("Unload")) { UnloadScene(state, sceneList[selectedScene].second); }
        ImGui::EndDisabled();

        ImGui::SameLine();
        const bool currentSceneModified = std::ranges::find(state->modifiedScenes, state->currentSceneId) != state->modifiedScenes.end();
        if (currentSceneModified) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 1.0f)); }
        if (ImGui::Button(currentSceneModified ? "Save*" : "Save")) {
            SaveSceneToFile(state->currentSceneId, state->currentSceneName, state, ctx->assetManager, ctx);
            std::erase(state->modifiedScenes, state->currentSceneId);
        }
        if (currentSceneModified) { ImGui::PopStyleColor(); }

        ImGui::SameLine();
        if (ImGui::Checkbox("Auto", &state->bAutoSave)) {
            state->autoSaveTimer = 0.0f;
        }
        if (state->bAutoSave && ImGui::IsItemHovered()) {
            float remaining = state->autoSaveInterval - state->autoSaveTimer;
            ImGui::SetTooltip("Auto-save in %.0fs", remaining);
        }

        ImGui::SameLine();
        if (ImGui::Button("New")) {
            state->currentSceneId = StringID{state->rng()};
            state->currentSceneName = "New Scene";
        }

        char sceneName[128];
        strncpy_s(sceneName, state->currentSceneName.c_str(), sizeof(sceneName) - 1);
        sceneName[sizeof(sceneName) - 1] = '\0';
        if (ImGui::InputText("Name", sceneName, sizeof(sceneName))) {
            state->currentSceneName = sceneName;
        }

        ImGui::BeginDisabled(true);
        ImGui::Text("ID: %llu", state->currentSceneId.id);
        ImGui::EndDisabled();

        ImGui::SeparatorText("Spawn Model");

        const auto& modelCache = ctx->assetManager->GetModelCache();
        static int selectedModel = 0;
        std::vector<std::pair<std::string, Engine::ModelID>> modelList;
        modelList.reserve(modelCache.size());
        for (const auto& [id, meta] : modelCache) {
            modelList.emplace_back(meta.name, id);
        }

        if (!modelList.empty()) {
            std::ranges::sort(modelList, {}, &std::pair<std::string, Engine::ModelID>::first);
            selectedModel = std::clamp(selectedModel, 0, static_cast<int>(modelList.size()) - 1);
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(modelList.empty());
        if (ImGui::BeginCombo("##model_list", modelList.empty() ? "No models" : modelList[selectedModel].first.c_str())) {
            for (int i = 0; i < static_cast<int>(modelList.size()); ++i) {
                bool sel = (i == selectedModel);
                if (ImGui::Selectable(modelList[i].first.c_str(), sel)) {
                    selectedModel = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(modelList.empty());
        if (ImGui::Button("Spawn")) {
            glm::vec3 offset = cameraPos + normalize(cameraFwd) * 5.0f;
            auto spawned = SpawnModel(state, ctx->assetManager, modelList[selectedModel].second, offset);
            if (!spawned.empty()) {
                state->selectedEntities = spawned;
                MarkSceneModified(state, state->currentSceneId);
            }
        }
        ImGui::EndDisabled();

        ImGui::NewLine();

        ImGui::SeparatorText("Entities");
        if (ImGui::Button("Create Entity")) {
            auto newEntity = CreateSceneEntity(state);
            state->selectedEntities = {newEntity};
            MarkSceneModified(state, state->currentSceneId);
        }
        static char search[64] = {};
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##search", search, sizeof(search));

        entt::entity entityToDelete = entt::null;
        auto view2 = state->registry.view<Component::SceneComponent>();
        for (auto entity : view2) {
            auto& scene = view2.get<Component::SceneComponent>(entity);
            if (scene.sceneId != state->currentSceneId) continue;

            const char* label = "Unnamed";
            if (auto* name = state->registry.try_get<Component::NameComponent>(entity))
                label = name->name.c_str();

            if (search[0] && !strstr(label, search)) continue;

            auto* stable = state->registry.try_get<Component::StableIdComponent>(entity);
            uint64_t stableId = stable ? stable->id.id : static_cast<uint64_t>(entity);

            char uniqueLabel[256];
            snprintf(uniqueLabel, sizeof(uniqueLabel), "%s##%llu", label, stableId);

            bool selected = std::find(state->selectedEntities.begin(), state->selectedEntities.end(), entity) != state->selectedEntities.end();

            if (ImGui::SmallButton(fmt::format("X##{}", stableId).c_str())) {
                entityToDelete = entity;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(fmt::format("C##{}", stableId).c_str())) {
                entt::entity copied = CopySceneEntity(state, entity, state->currentSceneId);
                state->selectedEntities = {copied};
                MarkSceneModified(state, state->currentSceneId);
            }
            ImGui::SameLine();
            if (ImGui::Selectable(uniqueLabel, selected)) {
                if (ImGui::GetIO().KeyCtrl) {
                    auto pos = std::find(state->selectedEntities.begin(), state->selectedEntities.end(), entity);
                    if (pos != state->selectedEntities.end())
                        state->selectedEntities.erase(pos);
                    else
                        state->selectedEntities.push_back(entity);
                }
                else {
                    state->selectedEntities = {entity};
                }
            }
        }
        if (entityToDelete != entt::null) {
            Component::StableIdComponent stableId = state->registry.get<Component::StableIdComponent>(entityToDelete);
            for (auto& entry : state->componentRegistry.registry) {
                if (entry.has(state->registry, entityToDelete)) {
                    entry.onRemoveComponent(state->registry, entityToDelete);
                }
            }
            auto it = std::ranges::find(state->selectedEntities, entityToDelete);
            if (it != state->selectedEntities.end()) {
                state->selectedEntities.erase(it);
            }
            state->stableIdToEntityMap.erase(stableId.id);
            state->registry.destroy(entityToDelete);
            MarkSceneModified(state, state->currentSceneId);
        }
    }
    ImGui::End();

    glm::vec3 multiGizmoCentroid{0.0f};
    int transformCount = 0;
    for (auto entity : state->selectedEntities) {
        if (auto* tf = state->registry.try_get<Component::TransformComponent>(entity)) {
            multiGizmoCentroid += tf->translation;
            ++transformCount;
        }
    }
    if (transformCount > 0)
        multiGizmoCentroid /= static_cast<float>(transformCount);

    if (ImGui::Begin("Details")) {
        if (state->selectedEntities.size() == 1) {
            ComponentEntry* entryToRemove = nullptr;
            entt::entity entity = state->selectedEntities[0];
            ImGui::Text("Entity: %u", static_cast<uint32_t>(entity));

            state->bCustomGizmoActivePrev = state->bCustomGizmoActive;
            state->bCustomGizmoActive = false;
            auto* entityScene = state->registry.try_get<Component::SceneComponent>(entity);
            for (ComponentEntry& entry : state->componentRegistry.registry) {
                if (entry.has(state->registry, entity)) {
                    nlohmann::json before;
                    entry.serialize(state->registry, entity, before);

                    ComponentEditorResult result = entry.drawEditor(frameBuffer->mainViewFamily, state->registry, entity, entry.name);

                    if (result.requestRemoval) {
                        entryToRemove = &entry;
                        if (entityScene) MarkSceneModified(state, entityScene->sceneId);
                    } else {
                        nlohmann::json after;
                        entry.serialize(state->registry, entity, after);
                        if (before != after && entityScene) MarkSceneModified(state, entityScene->sceneId);
                    }
                }
            }
            if (entryToRemove) {
                entryToRemove->onRemoveComponent(state->registry, entity);
            }

            ImGui::Spacing();
            ImGui::SetNextWindowSize(ImVec2(250, 0));
            if (ImGui::Button("Add Component"))
                ImGui::OpenPopup("add_component_popup");

            if (ImGui::BeginPopup("add_component_popup")) {
                static char compSearch[64] = {};
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##compsearch", compSearch, sizeof(compSearch));

                for (auto& entry : state->componentRegistry.registry) {
                    if (compSearch[0] && !strstr(entry.name, compSearch)) { continue; }
                    bool disabled = entry.has(state->registry, entity) || !entry.canAdd(state->registry, entity);
                    if (disabled) {
                        ImGui::BeginDisabled(true);
                        ImGui::MenuItem(entry.name);
                        ImGui::EndDisabled();
                    }
                    else {
                        if (ImGui::MenuItem(entry.name)) {
                            CreateComponent(state, entity, entry.typeId);
                            MarkSceneModified(state, state->currentSceneId);
                        }
                    }
                }
                ImGui::EndPopup();
            }
        }
        else if (multiSelected) {
            ImGui::Text("%zu entities selected", state->selectedEntities.size());
            ImGui::Text("Ctrl+Click to add/remove entities");
            if (transformCount > 0)
                ImGui::Text("Centroid: (%.2f, %.2f, %.2f)", multiGizmoCentroid.x, multiGizmoCentroid.y, multiGizmoCentroid.z);
        }
    }
    ImGui::End();

    if (!state->bCustomGizmoActive && !state->selectedEntities.empty()) {
        if (state->selectedEntities.size() == 1) {
            entt::entity entity = state->selectedEntities[0];
            if (auto* transform = state->registry.try_get<Component::TransformComponent>(entity)) {
                auto model = Component::GetMatrix(*transform);
                float snapArr[3] = {};
                float* snap = nullptr;
                if (state->bSnapEnabled) {
                    if (state->currentGizmoOperation == ImGuizmo::TRANSLATE)
                        snapArr[0] = snapArr[1] = snapArr[2] = state->snapTranslation;
                    else if (state->currentGizmoOperation == ImGuizmo::ROTATE)
                        snapArr[0] = snapArr[1] = snapArr[2] = state->snapRotation;
                    else
                        snapArr[0] = snapArr[1] = snapArr[2] = state->snapScale;
                    snap = snapArr;
                }

                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(proj),
                    state->currentGizmoOperation,
                    state->currentGizmoMode,
                    glm::value_ptr(model),
                    nullptr,
                    snap
                );
                if (ImGuizmo::IsUsing()) {
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), t, r, s);
                    glm::vec3 translation = glm::vec3(t[0], t[1], t[2]);
                    if (state->bSnapEnabled && state->bSnapWorldGrid && state->currentGizmoOperation == ImGuizmo::TRANSLATE) {
                        const float g = state->snapTranslation;
                        translation = glm::round(translation / g) * g;
                    }
                    transform->translation = translation;
                    transform->rotation = glm::quat(glm::radians(glm::vec3(r[0], r[1], r[2])));
                    if (state->bUniformScaleMode)
                        transform->scale = glm::vec3((s[0] + s[1] + s[2]) / 3.0f);
                    else
                        transform->scale = glm::vec3(s[0], s[1], s[2]);
                    state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
                    if (auto* sc = state->registry.try_get<Component::SceneComponent>(entity)) {
                        MarkSceneModified(state, sc->sceneId);
                    }
                }
            }
        }
        else if (state->currentGizmoOperation != ImGuizmo::SCALE) {
            static glm::vec3 s_prevTranslation{};
            static bool s_wasDragging = false;

            float snapArr[3] = {};
            float* snap = nullptr;
            if (state->bSnapEnabled) {
                if (state->currentGizmoOperation == ImGuizmo::TRANSLATE)
                    snapArr[0] = snapArr[1] = snapArr[2] = state->snapTranslation;
                else
                    snapArr[0] = snapArr[1] = snapArr[2] = state->snapRotation;
                snap = snapArr;
            }

            glm::mat4 gizmoMatrix = glm::translate(glm::mat4(1.0f), multiGizmoCentroid);
            glm::mat4 deltaMatrix(1.0f);
            ImGuizmo::Manipulate(
                glm::value_ptr(view),
                glm::value_ptr(proj),
                state->currentGizmoOperation,
                ImGuizmo::WORLD,
                glm::value_ptr(gizmoMatrix),
                glm::value_ptr(deltaMatrix),
                snap
            );

            if (ImGuizmo::IsUsing()) {
                if (!s_wasDragging) {
                    s_prevTranslation = multiGizmoCentroid;
                    s_wasDragging = true;
                    for (auto e : state->selectedEntities) {
                        if (auto* sc = state->registry.try_get<Component::SceneComponent>(e)) {
                            MarkSceneModified(state, sc->sceneId);
                        }
                    }
                }

                glm::vec3 deltaTranslation{0.0f};
                if (state->currentGizmoOperation == ImGuizmo::TRANSLATE) {
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmoMatrix), t, r, s);
                    glm::vec3 newT = {t[0], t[1], t[2]};
                    if (state->bSnapEnabled && state->bSnapWorldGrid) {
                        const float g = state->snapTranslation;
                        newT = glm::round(newT / g) * g;
                    }
                    deltaTranslation = newT - s_prevTranslation;
                    s_prevTranslation = newT;
                }

                const glm::quat deltaRotation = glm::normalize(glm::quat_cast(glm::mat3(deltaMatrix)));

                for (auto entity : state->selectedEntities) {
                    auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
                    if (!transform) continue;

                    transform->translation += deltaTranslation;

                    const glm::vec3 rel = transform->translation - multiGizmoCentroid;
                    transform->translation = multiGizmoCentroid + deltaRotation * rel;
                    transform->rotation = deltaRotation * transform->rotation;

                    state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
                    state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(entity);
                }
            }
            else {
                s_wasDragging = false;
                s_prevTranslation = {};
            }
        }
    }

    if (ImGui::Begin("Post-Processing")) {
        constexpr Core::PostProcessConfiguration defaultPP{};
        if (ImGui::Button("Reset All to Defaults")) {
            state->postProcess = defaultPP;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable All Effects")) {
            state->postProcess.bEnableTemporalAntialiasing = false;
            state->postProcess.tonemapOperator = -1;
            state->postProcess.bloomIntensity = 0.0f;
            state->postProcess.motionBlurVelocityScale = 0.0f;
            state->postProcess.chromaticAberrationStrength = 0.0f;
            state->postProcess.vignetteStrength = 0.0f;
            state->postProcess.grainStrength = 0.0f;
            state->postProcess.sharpeningStrength = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Ground Truth Ambient Occlusion");
        ImGui::Checkbox("Enable GTAO", &state->gtaoConfig.bEnabled);

        ImGui::Spacing();
        ImGui::SeparatorText("Anti-Aliasing");
        ImGui::Checkbox("Enable TAA", &state->postProcess.bEnableTemporalAntialiasing);

        ImGui::Spacing();
        ImGui::SeparatorText("Tonemapping");
        const char* tonemapOperators[] = {"None", "ACES", "Uncharted 2", "Reinhard", "Lottes"};
        int currentItem = state->postProcess.tonemapOperator + 1;
        if (ImGui::Combo("Operator", &currentItem, tonemapOperators, IM_ARRAYSIZE(tonemapOperators))) {
            state->postProcess.tonemapOperator = currentItem - 1;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Exposure");
        ImGui::SliderFloat("Target Luminance", &state->postProcess.exposureTargetLuminance, 0.01f, 1.0f, "%.3f");
        ImGui::SliderFloat("Adaptation Speed", &state->postProcess.exposureAdaptationRate, 0.1f, 50.0f, "%.1f");
        if (ImGui::Button("Reset Exposure")) {
            state->postProcess.exposureTargetLuminance = defaultPP.exposureTargetLuminance;
            state->postProcess.exposureAdaptationRate = defaultPP.exposureAdaptationRate;
        }


        ImGui::Spacing();
        ImGui::SeparatorText("Bloom");
        ImGui::SliderFloat("Intensity", &state->postProcess.bloomIntensity, 0.0f, 0.2f, "%.3f");
        ImGui::SliderFloat("Threshold", &state->postProcess.bloomThreshold, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Soft Threshold", &state->postProcess.bloomSoftThreshold, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Radius", &state->postProcess.bloomRadius, 0.5f, 2.0f, "%.2f");
        if (ImGui::Button("Reset Bloom")) {
            state->postProcess.bloomIntensity = defaultPP.bloomIntensity;
            state->postProcess.bloomThreshold = defaultPP.bloomThreshold;
            state->postProcess.bloomSoftThreshold = defaultPP.bloomSoftThreshold;
            state->postProcess.bloomRadius = defaultPP.bloomRadius;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Bloom")) {
            state->postProcess.bloomIntensity = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Motion Blur");
        ImGui::DragFloat("Velocity Scale", &state->postProcess.motionBlurVelocityScale, 0.05f, 0.0f, 4.0f, "%.2f");
        ImGui::DragFloat("Depth Scale", &state->postProcess.motionBlurDepthScale, 0.1f, 2.0f, 10.0f, "%.2f");
        if (ImGui::Button("Reset Motion Blur")) {
            state->postProcess.motionBlurVelocityScale = defaultPP.motionBlurVelocityScale;
            state->postProcess.motionBlurDepthScale = defaultPP.motionBlurDepthScale;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Motion Blur")) {
            state->postProcess.motionBlurVelocityScale = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Color Grading");
        ImGui::SliderFloat("Exposure Offset", &state->postProcess.colorGradingExposure, -2.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Contrast", &state->postProcess.colorGradingContrast, 0.5f, 2.0f, "%.2f");
        ImGui::SliderFloat("Saturation", &state->postProcess.colorGradingSaturation, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Temperature", &state->postProcess.colorGradingTemperature, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Tint", &state->postProcess.colorGradingTint, -1.0f, 1.0f, "%.2f");
        if (ImGui::Button("Reset Color Grading")) {
            state->postProcess.colorGradingExposure = defaultPP.colorGradingExposure;
            state->postProcess.colorGradingContrast = defaultPP.colorGradingContrast;
            state->postProcess.colorGradingSaturation = defaultPP.colorGradingSaturation;
            state->postProcess.colorGradingTemperature = defaultPP.colorGradingTemperature;
            state->postProcess.colorGradingTint = defaultPP.colorGradingTint;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Chromatic Aberration");
        ImGui::SliderFloat("Aberration Strength", &state->postProcess.chromaticAberrationStrength, 0.0f, 100.0f, "%.2f");
        if (ImGui::Button("Reset Aberration")) {
            state->postProcess.chromaticAberrationStrength = defaultPP.chromaticAberrationStrength;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Aberration")) {
            state->postProcess.chromaticAberrationStrength = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Vignette");
        ImGui::SliderFloat("Vignette Strength", &state->postProcess.vignetteStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Vignette Radius", &state->postProcess.vignetteRadius, 0.5f, 1.0f, "%.2f");
        ImGui::SliderFloat("Vignette Smoothness", &state->postProcess.vignetteSmoothness, 0.1f, 1.0f, "%.2f");
        if (ImGui::Button("Reset Vignette")) {
            state->postProcess.vignetteStrength = defaultPP.vignetteStrength;
            state->postProcess.vignetteRadius = defaultPP.vignetteRadius;
            state->postProcess.vignetteSmoothness = defaultPP.vignetteSmoothness;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Vignette")) {
            state->postProcess.vignetteStrength = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Film Grain");
        ImGui::SliderFloat("Grain Strength", &state->postProcess.grainStrength, 0.0f, 0.15f, "%.3f");
        ImGui::SliderFloat("Grain Size", &state->postProcess.grainSize, 1.0f, 3.0f, "%.2f");
        if (ImGui::Button("Reset Grain")) {
            state->postProcess.grainStrength = defaultPP.grainStrength;
            state->postProcess.grainSize = defaultPP.grainSize;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Grain")) {
            state->postProcess.grainStrength = 0.0f;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Sharpening");
        ImGui::SliderFloat("Sharpening Strength", &state->postProcess.sharpeningStrength, 0.0f, 100.0f, "%.02f");
        if (ImGui::Button("Reset Sharpening")) {
            state->postProcess.sharpeningStrength = defaultPP.sharpeningStrength;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable Sharpening")) {
            state->postProcess.sharpeningStrength = 0.0f;
        }
    }
    ImGui::End();

    if (ImGui::Begin("Scene")) {
        ImGui::Checkbox("Enable Physics", &state->bEnablePhysics);

        if (ImGui::CollapsingHeader("Directional Light")) {
            ImGui::SliderFloat3("Direction", &state->directionalLight.direction.x, -1.0f, 1.0f);
            if (ImGui::Button("Normalize Direction")) {
                frameBuffer->mainViewFamily.directionalLight.direction = glm::normalize(state->directionalLight.direction);
            }
            ImGui::SliderFloat("Intensity", &state->directionalLight.intensity, 0.0f, 5.0f);
            ImGui::ColorEdit3("Color", &state->directionalLight.color.x);
        }

        if (ImGui::CollapsingHeader("Shadow Settings")) {
            const char* qualityNames[] = {"Ultra", "High", "Medium", "Low", "Custom"};
            int currentQuality = static_cast<int>(state->shadowQuality);
            if (ImGui::Combo("Quality", &currentQuality, qualityNames, 5)) {
                state->shadowQuality = static_cast<Core::ShadowQuality>(currentQuality);
                if (currentQuality < 4) {
                    state->shadowConfig.cascadePreset = Render::SHADOW_PRESETS[currentQuality];
                }
            }

            ImGui::SliderFloat("Shadow Intensity", &state->shadowConfig.shadowIntensity, 0.0f, 1.0f);

            ImGui::Separator();
            ImGui::Text("Current Configuration:");
            for (int i = 0; i < 4; ++i) {
                ImGui::Text("Cascade %d:", i);
                ImGui::Indent();
                ImGui::Text("  Resolution: %dx%d",
                            state->shadowConfig.cascadePreset.extents[i].width,
                            state->shadowConfig.cascadePreset.extents[i].height);
                ImGui::Text("  Bias: %.2f/%.2f",
                            state->shadowConfig.cascadePreset.biases[i].linear,
                            state->shadowConfig.cascadePreset.biases[i].sloped);
                ImGui::Text("  PCSS Samples: %u blocker, %u PCF",
                            state->shadowConfig.cascadePreset.pcssSamples[i].blockerSearchSamples,
                            state->shadowConfig.cascadePreset.pcssSamples[i].pcfSamples);
                ImGui::Text("  Light Size: %.4f",
                            state->shadowConfig.cascadePreset.lightSizes[i]);
                ImGui::Unindent();
            }

            if (state->shadowQuality == Core::ShadowQuality::Custom) {
                ImGui::Separator();
                ImGui::Text("Custom Settings:");

                static Render::ShadowCascadePreset customPreset = state->shadowConfig.cascadePreset;

                for (int i = 0; i < 4; ++i) {
                    ImGui::PushID(i);
                    if (ImGui::TreeNode("Cascade", "Cascade %d", i)) {
                        ImGui::InputInt("Width", reinterpret_cast<int*>(&customPreset.extents[i].width));
                        ImGui::InputInt("Height", reinterpret_cast<int*>(&customPreset.extents[i].height));
                        ImGui::InputFloat("Linear Bias", &customPreset.biases[i].linear);
                        ImGui::InputFloat("Sloped Bias", &customPreset.biases[i].sloped);
                        ImGui::InputScalar("Blocker Samples", ImGuiDataType_U32, &customPreset.pcssSamples[i].blockerSearchSamples);
                        ImGui::InputScalar("PCF Samples", ImGuiDataType_U32, &customPreset.pcssSamples[i].pcfSamples);
                        ImGui::InputFloat("Light Size", &customPreset.lightSizes[i]);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }

                if (ImGui::Button("Apply Custom Settings")) {
                    state->shadowConfig.cascadePreset = customPreset;
                }
            }

            ImGui::Separator();
            ImGui::SliderFloat("Split Lambda", &state->shadowConfig.splitLambda, 0.0f, 1.0f);
            ImGui::SliderFloat("Split Overlap", &state->shadowConfig.splitOverlap, 1.0f, 1.2f);
            ImGui::Checkbox("Enabled", &state->shadowConfig.enabled);
        }
    }
    ImGui::End();

    if (ImGui::Begin("Materials")) {
        Engine::MaterialManager* materialManager = ctx->materialManager;

        static char newMatName[128] = "new_material";
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##matname", newMatName, sizeof(newMatName));
        ImGui::SameLine();
        const bool nameEmpty = newMatName[0] == '\0';
        const bool nameExists = !nameEmpty && materialManager->FindMutableMaterial(StringID{newMatName, strlen(newMatName)}).IsValid();
        ImGui::BeginDisabled(nameEmpty || nameExists);
        if (ImGui::Button("Create Material")) {
            materialManager->CreateMaterial(newMatName);
        }
        ImGui::EndDisabled();
        if (nameExists) {
            ImGui::SameLine();
            ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
        }

        const auto& allMaterials = materialManager->GetMaterials();
        int32_t mutableCount = 0;
        for (const auto& [id, mat] : allMaterials) {
            if (!mat.immutable) ++mutableCount;
        }
        ImGui::SeparatorText(fmt::format("Materials ({})", mutableCount).c_str());

        for (const auto& [id, mat] : allMaterials) {
            if (mat.immutable) continue;
            ImGui::PushID(static_cast<int>(id.id));
            if (ImGui::CollapsingHeader(mat.name.c_str())) {
                ImGui::BeginDisabled(true);
                ImGui::Text("ID: %llu", id.id);
                ImGui::EndDisabled();

                Engine::Material editMat = mat;
                MaterialProperties& props = editMat.props;
                bool changed = false;

                ImGui::SeparatorText("Base");
                changed |= ImGui::ColorEdit4("Color Factor", &props.colorFactor.x);
                changed |= ImGui::SliderFloat("Metallic", &props.metalRoughFactors.x, 0.0f, 1.0f);
                changed |= ImGui::SliderFloat("Roughness", &props.metalRoughFactors.y, 0.0f, 1.0f);

                ImGui::SeparatorText("Emissive");
                changed |= ImGui::ColorEdit3("Emissive Color", &props.emissiveFactor.x);
                changed |= ImGui::DragFloat("Emissive Strength", &props.emissiveFactor.w, 0.01f, 0.0f, 100.0f);

                ImGui::SeparatorText("Alpha");
                const char* alphaModes[] = {"Opaque", "Mask", "Blend"};
                int alphaMode = static_cast<int>(props.alphaProperties.y);
                if (ImGui::Combo("Alpha Mode", &alphaMode, alphaModes, 3)) {
                    props.alphaProperties.y = static_cast<float>(alphaMode);
                    changed = true;
                }
                if (alphaMode == 1) {
                    changed |= ImGui::SliderFloat("Alpha Cutoff", &props.alphaProperties.x, 0.0f, 1.0f);
                }
                bool doubleSided = props.alphaProperties.z > 0.5f;
                if (ImGui::Checkbox("Double Sided", &doubleSided)) {
                    props.alphaProperties.z = doubleSided ? 1.0f : 0.0f;
                    changed = true;
                }
                bool unlit = props.alphaProperties.w > 0.5f;
                if (ImGui::Checkbox("Unlit", &unlit)) {
                    props.alphaProperties.w = unlit ? 1.0f : 0.0f;
                    changed = true;
                }

                ImGui::SeparatorText("Physical");
                changed |= ImGui::SliderFloat("IOR", &props.physicalProperties.x, 1.0f, 3.0f);
                changed |= ImGui::SliderFloat("Normal Intensity", &props.physicalProperties.z, 0.0f, 2.0f);
                changed |= ImGui::SliderFloat("Occlusion Strength", &props.physicalProperties.w, 0.0f, 1.0f);

                if (ImGui::TreeNode("UV Transforms")) {
                    ImGui::SeparatorText("Color");
                    changed |= ImGui::DragFloat2("Scale##color_uv", &props.colorUvTransform.x, 0.01f);
                    changed |= ImGui::DragFloat2("Offset##color_uv", &props.colorUvTransform.z, 0.01f);
                    ImGui::SeparatorText("Metal/Rough");
                    changed |= ImGui::DragFloat2("Scale##mr_uv", &props.metalRoughUvTransform.x, 0.01f);
                    changed |= ImGui::DragFloat2("Offset##mr_uv", &props.metalRoughUvTransform.z, 0.01f);
                    ImGui::SeparatorText("Normal");
                    changed |= ImGui::DragFloat2("Scale##normal_uv", &props.normalUvTransform.x, 0.01f);
                    changed |= ImGui::DragFloat2("Offset##normal_uv", &props.normalUvTransform.z, 0.01f);
                    ImGui::SeparatorText("Emissive");
                    changed |= ImGui::DragFloat2("Scale##emissive_uv", &props.emissiveUvTransform.x, 0.01f);
                    changed |= ImGui::DragFloat2("Offset##emissive_uv", &props.emissiveUvTransform.z, 0.01f);
                    ImGui::SeparatorText("Occlusion");
                    changed |= ImGui::DragFloat2("Scale##occlusion_uv", &props.occlusionUvTransform.x, 0.01f);
                    changed |= ImGui::DragFloat2("Offset##occlusion_uv", &props.occlusionUvTransform.z, 0.01f);
                    ImGui::TreePop();
                }

                ImGui::SeparatorText("Textures");
                static const char* slotNames[] = {"Color", "Metal/Rough", "Normal", "Emissive", "Occlusion", "Packed NRM"};
                static Engine::TextureID texEditPending = Engine::TextureID::INVALID;
                static Engine::SamplerDesc samplerEditPending{};

                const auto& texNameToId = ctx->assetManager->GetTextureNameToId();
                const auto& texCache = ctx->assetManager->GetTextureCache();

                for (int32_t slot = 0; slot < 6; ++slot) {
                    ImGui::PushID(slot);

                    const Engine::TextureID& texId = mat.textureRefs[slot];
                    const char* currentTexName = "None";
                    if (texId.IsValid()) {
                        auto it = texCache.find(texId);
                        if (it != texCache.end()) currentTexName = it->second.name;
                    }

                    ImGui::Text("%-13s", slotNames[slot]);
                    ImGui::SameLine();
                    ImGui::Text("%-32s", currentTexName);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Tex")) {
                        texEditPending = texId;
                        ImGui::OpenPopup("TextureSelect");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Smp")) {
                        samplerEditPending = mat.samplerDesc[slot];
                        ImGui::OpenPopup("SamplerEdit");
                    }

                    if (ImGui::BeginPopupModal("TextureSelect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        static Engine::TextureID previewId = Engine::TextureID::INVALID;

                        ImGui::Text("Slot: %s", slotNames[slot]);
                        ImGui::Separator();

                        if (ImGui::BeginChild("##texlist", {400.0f, 300.0f}, ImGuiChildFlags_Borders)) {
                            bool noneSelected = !texEditPending.IsValid();
                            if (ImGui::Selectable("(None)", noneSelected)) {
                                texEditPending = Engine::TextureID::INVALID;
                            }
                            std::vector<std::pair<std::string, Engine::TextureID> > sorted(texNameToId.begin(), texNameToId.end());
                            std::sort(sorted.begin(), sorted.end());
                            for (const auto& [name, tid] : sorted) {
                                bool selected = texEditPending == tid;
                                if (ImGui::Selectable(name.c_str(), selected)) {
                                    texEditPending = tid;
                                }
                                if (ImGui::IsItemHovered()) {
                                    if (previewId != tid) {
                                        if (previewId.IsValid()) state->texResidency.Release(previewId, ctx);
                                        previewId = tid;
                                        // todo: ideally only load the lowest mip for preview
                                        state->texResidency.Acquire(tid, ctx);
                                    }
                                    ImGui::BeginTooltip();
                                    uint64_t ds = state->texResidency.GetDescSet(tid, ctx);
                                    if (ds) {
                                        ImGui::Image(ds, {128.0f, 128.0f});
                                    }
                                    else {
                                        ImGui::Text("Loading...");
                                    }
                                    ImGui::EndTooltip();
                                }
                            }
                        }
                        ImGui::EndChild();

                        if (ImGui::Button("OK")) {
                            Engine::Material _editMat = mat;
                            _editMat.textureRefs[slot] = texEditPending;
                            materialManager->UpdateMutableMaterial(id, _editMat, true);
                            if (previewId.IsValid()) {
                                state->texResidency.Release(previewId, ctx);
                                previewId = Engine::TextureID::INVALID;
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel") || state->inputFrame->GetKey(Key::ESCAPE).pressed) {
                            if (previewId.IsValid()) {
                                state->texResidency.Release(previewId, ctx);
                                previewId = Engine::TextureID::INVALID;
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginPopupModal("SamplerEdit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("Slot: %s", slotNames[slot]);
                        ImGui::Separator();

                        static const char* filterNames[] = {"Nearest", "Linear"};
                        int magF = static_cast<int>(samplerEditPending.magFilter);
                        int minF = static_cast<int>(samplerEditPending.minFilter);
                        if (ImGui::Combo("Mag Filter", &magF, filterNames, 2)) samplerEditPending.magFilter = static_cast<VkFilter>(magF);
                        if (ImGui::Combo("Min Filter", &minF, filterNames, 2)) samplerEditPending.minFilter = static_cast<VkFilter>(minF);

                        static const char* mipmapModeNames[] = {"Nearest", "Linear"};
                        int mipMode = static_cast<int>(samplerEditPending.mipmapMode);
                        if (ImGui::Combo("Mip Mode", &mipMode, mipmapModeNames, 2)) samplerEditPending.mipmapMode = static_cast<VkSamplerMipmapMode>(mipMode);

                        static const char* addressModeNames[] = {"Repeat", "Mirrored Repeat", "Clamp To Edge", "Clamp To Border", "Mirror Clamp"};
                        int addrU = static_cast<int>(samplerEditPending.addressModeU);
                        int addrV = static_cast<int>(samplerEditPending.addressModeV);
                        int addrW = static_cast<int>(samplerEditPending.addressModeW);
                        if (ImGui::Combo("Address U", &addrU, addressModeNames, 5)) samplerEditPending.addressModeU = static_cast<VkSamplerAddressMode>(addrU);
                        if (ImGui::Combo("Address V", &addrV, addressModeNames, 5)) samplerEditPending.addressModeV = static_cast<VkSamplerAddressMode>(addrV);
                        if (ImGui::Combo("Address W", &addrW, addressModeNames, 5)) samplerEditPending.addressModeW = static_cast<VkSamplerAddressMode>(addrW);

                        bool aniso = samplerEditPending.anisotropyEnable == VK_TRUE;
                        if (ImGui::Checkbox("Anisotropy", &aniso)) samplerEditPending.anisotropyEnable = aniso ? VK_TRUE : VK_FALSE;
                        if (aniso) ImGui::SliderFloat("Max Anisotropy", &samplerEditPending.maxAnisotropy, 1.0f, 16.0f);

                        ImGui::DragFloat("Mip LOD Bias", &samplerEditPending.mipLodBias, 0.1f);
                        ImGui::DragFloat("Min LOD", &samplerEditPending.minLod, 0.1f, 0.0f, 100.0f);
                        ImGui::DragFloat("Max LOD", &samplerEditPending.maxLod, 0.1f, 0.0f, 1000.0f);

                        if (ImGui::Button("OK")) {
                            Engine::Material _editMat = mat;
                            _editMat.samplerDesc[slot] = samplerEditPending;
                            materialManager->UpdateMutableMaterial(id, _editMat, true);
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel") || state->inputFrame->GetKey(Key::ESCAPE).pressed) {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }

                if (changed) {
                    materialManager->UpdateMutableMaterial(id, editMat, true);
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::End();

    frameBuffer->mainViewFamily.directionalLight = state->directionalLight;
    frameBuffer->mainViewFamily.shadowConfig = state->shadowConfig;
    frameBuffer->mainViewFamily.postProcessConfig = state->postProcess;
    frameBuffer->mainViewFamily.gtaoConfig = state->gtaoConfig;
    frameBuffer->mainViewFamily.debugResourceName = state->debugResourceName;
    frameBuffer->mainViewFamily.debugTransformationType = state->debugTransformationType;
    frameBuffer->mainViewFamily.debugViewAspect = state->debugViewAspect;
}
}
