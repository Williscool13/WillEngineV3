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
#include "game/fwd_components.h"
#include "game/components/common_components.h"
#include "game/components/scene_components.h"

namespace Game
{
void EditorUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (state->bIsPlaying) {
        if (state->inputFrame->GetKey(Key::ESCAPE).pressed) {
            PlayStop(ctx, state);
        }
        return;
    }

    if (ctx->bImGuiWantsTextInput) { return; }

    const bool ctrlHeld = state->inputFrame->GetKey(Key::LCTRL).down || state->inputFrame->GetKey(Key::RCTRL).down;

    if (!ctrlHeld) {
        if (state->inputFrame->GetKey(Key::W).pressed) {
            state->currentGizmoOperation = ImGuizmo::TRANSLATE;
        }
        else if (state->inputFrame->GetKey(Key::E).pressed) {
            state->currentGizmoOperation = ImGuizmo::ROTATE;
        }
        else if (state->inputFrame->GetKey(Key::R).pressed) {
            state->currentGizmoOperation = ImGuizmo::SCALE;
        }
    }

    if (ctrlHeld && state->inputFrame->GetKey(Key::W).pressed) {
        state->bWantCopyEntities = true;
    }

    if (state->inputFrame->GetKey(Key::DEL).pressed) {
        state->bWantDeleteEntities = true;
    }

    if (state->inputFrame->GetKey(Key::ESCAPE).pressed) {
        state->selectedEntities.clear();
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

        glm::mat4 viewCopy = frameBuffer->mainViewFamily.mainView.currentViewData.view;
        ImGuizmo::ViewManipulate(glm::value_ptr(viewCopy), 8.0f, ImVec2(vpRight - gizmoSize, vpTop), ImVec2(gizmoSize, gizmoSize), 0x10101080);

        if (viewCopy != frameBuffer->mainViewFamily.mainView.currentViewData.view) {
            const glm::mat4 invView = glm::inverse(viewCopy);
            const glm::vec3 newForward = -glm::normalize(glm::vec3(invView[2]));
            const glm::vec3 newUp = glm::normalize(glm::vec3(invView[1]));

            editorCameraTransform.rotation = glm::normalize(glm::quat_cast(glm::mat3(invView)));

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
        if (ImGui::RadioButton("S##gizmo_op", state->currentGizmoOperation == ImGuizmo::SCALE)) { state->currentGizmoOperation = ImGuizmo::SCALE; }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Scale (R)"); }

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
    }
    ImGui::End();

    if (ImGui::Begin("Scene Browser")) {
        const auto& sceneReg = ctx->assetManager->GetSceneRegistry();
        static int selectedScene = 0;
        std::vector<std::pair<std::string, StringID>> sceneList;
        sceneList.reserve(sceneReg.size());
        for (const auto& [id, path] : sceneReg) {
            sceneList.emplace_back(path.stem().string(), id);
        }
        std::ranges::sort(sceneList, {}, &std::pair<std::string, StringID>::first);
        selectedScene = std::clamp(selectedScene, 0, static_cast<int>(sceneList.size()) - 1);

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
        if (ImGui::Button("Save")) { SaveSceneToFile(state->currentSceneId, state->currentSceneName, state, ctx->assetManager); }

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

        const auto& modelReg = ctx->assetManager->GetModelRegistry();
        static int selectedModel = 0;
        std::vector<std::pair<std::string, StringID> > modelList;
        modelList.reserve(modelReg.size());
        for (const auto& [id, path] : modelReg) {
            modelList.emplace_back(path.stem().string(), id);
        }
        std::ranges::sort(modelList, {}, &std::pair<std::string, StringID>::first);
        selectedModel = std::clamp(selectedModel, 0, static_cast<int>(modelList.size()) - 1);

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##model_list", modelList.empty() ? "" : modelList[selectedModel].first.c_str())) {
            for (int i = 0; i < static_cast<int>(modelList.size()); ++i) {
                bool sel = (i == selectedModel);
                if (ImGui::Selectable(modelList[i].first.c_str(), sel)) {
                    selectedModel = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::BeginDisabled(modelList.empty());
        if (ImGui::Button("Spawn")) {
            glm::vec3 offset = cameraPos + normalize(cameraFwd) * 5.0f;
            auto spawned = SpawnModel(state, ctx->assetManager, modelList[selectedModel].second, offset);
            if (!spawned.empty()) {
                state->selectedEntities = spawned;
            }
        }
        ImGui::EndDisabled();

        ImGui::NewLine();

        ImGui::SeparatorText("Entities");
        if (ImGui::Button("Create Entity")) {
            auto newEntity = CreateSceneEntity(state);
            state->selectedEntities = {newEntity};
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
        }
    }
    ImGui::End();

    if (ImGui::Begin("Details")) {
        if (state->selectedEntities.size() == 1) {
            ComponentEntry* entryToRemove = nullptr;
            entt::entity entity = state->selectedEntities[0];
            ImGui::Text("Entity: %u", static_cast<uint32_t>(entity));

            state->bCustomGizmoActivePrev = state->bCustomGizmoActive;
            state->bCustomGizmoActive = false;
            for (ComponentEntry& entry : state->componentRegistry.registry) {
                if (entry.has(state->registry, entity)) {
                    ComponentEditorResult result = entry.drawEditor(frameBuffer->mainViewFamily, state->registry, entity, entry.name);
                    if (result.requestRemoval) {
                        entryToRemove = &entry;
                    }
                }
            }
            if (entryToRemove) {
                entryToRemove->onRemoveComponent(state->registry, entity);
            }

            if (!state->bCustomGizmoActive) {
                if (auto* transform = state->registry.try_get<Component::TransformComponent>(entity)) {
                    glm::mat4 model = Component::GetMatrix(*transform);
                    float snapArr[3] = {};
                    float* snap = nullptr;
                    if (state->bSnapEnabled) {
                        if (state->currentGizmoOperation == ImGuizmo::TRANSLATE) {
                            snapArr[0] = snapArr[1] = snapArr[2] = state->snapTranslation;
                        }
                        else if (state->currentGizmoOperation == ImGuizmo::ROTATE) {
                            snapArr[0] = snapArr[1] = snapArr[2] = state->snapRotation;
                        }
                        else {
                            snapArr[0] = snapArr[1] = snapArr[2] = state->snapScale;
                        }
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
                        transform->translation = glm::vec3(t[0], t[1], t[2]);
                        transform->rotation = glm::quat(glm::radians(glm::vec3(r[0], r[1], r[2])));
                        if (state->bUniformScaleMode) {
                            transform->scale = glm::vec3((s[0] + s[1] + s[2]) / 3.0f);
                        }
                        else {
                            transform->scale = glm::vec3(s[0], s[1], s[2]);
                        }
                        state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
                    }
                }
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
                    if (compSearch[0] && !strstr(entry.name, compSearch)) continue;
                    if (entry.has(state->registry, entity)) {
                        ImGui::BeginDisabled(true);
                        ImGui::MenuItem(entry.name);
                        ImGui::EndDisabled();
                    }
                    else {
                        if (ImGui::MenuItem(entry.name)) {
                            CreateComponent(state, entity, entry.typeId);
                            entry.onAddComponent(state->registry, entity);
                        }
                    }
                }
                ImGui::EndPopup();
            }
        }
        else if (multiSelected) {
            ImGui::Text("%zu entities selected", state->selectedEntities.size());
            ImGui::Text("Ctrl+Click to add/remove entities");

            // Compute centroid of all selected entities that have a transform
            glm::vec3 averagePos{0.0f};
            int transformCount = 0;
            for (auto entity : state->selectedEntities) {
                if (auto* tf = state->registry.try_get<Component::TransformComponent>(entity)) {
                    averagePos += tf->translation;
                    ++transformCount;
                }
            }

            if (transformCount > 0) {
                averagePos /= static_cast<float>(transformCount);
                ImGui::Text("Centroid: (%.2f, %.2f, %.2f)", averagePos.x, averagePos.y, averagePos.z);

                static glm::quat s_prevRotation{1.0f, 0.0f, 0.0f, 0.0f};
                static glm::vec3 s_prevScale{1.0f, 1.0f, 1.0f};

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
                glm::mat4 gizmoMatrix = glm::translate(glm::mat4(1.0f), averagePos);
                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(proj),
                    state->currentGizmoOperation,
                    ImGuizmo::WORLD,
                    glm::value_ptr(gizmoMatrix),
                    nullptr,
                    snap
                );

                if (ImGuizmo::IsUsing()) {
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmoMatrix), t, r, s);

                    const glm::vec3 newT = glm::vec3(t[0], t[1], t[2]);
                    const glm::quat newR = glm::quat(glm::radians(glm::vec3(r[0], r[1], r[2])));
                    const glm::vec3 newS = glm::vec3(s[0], s[1], s[2]);

                    const glm::vec3 deltaTranslation = newT - averagePos;
                    const glm::quat deltaRotation = newR * glm::conjugate(s_prevRotation);
                    const glm::vec3 deltaScale = newS / s_prevScale;

                    for (auto entity : state->selectedEntities) {
                        auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
                        if (!transform) continue;

                        transform->translation += deltaTranslation;

                        glm::vec3 rel = transform->translation - averagePos;
                        transform->translation = averagePos + deltaRotation * rel;
                        transform->rotation = deltaRotation * transform->rotation;

                        rel = transform->translation - averagePos;
                        transform->translation = averagePos + rel * deltaScale;
                        transform->scale *= deltaScale;

                        state->registry.emplace_or_replace<Component::DirtyRenderTransformComponent>(entity);
                        state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(entity);
                    }

                    s_prevRotation = newR;
                    s_prevScale = newS;
                }
                else {
                    // Reset each frame we're not dragging so the next drag starts from identity
                    s_prevRotation = {1.0f, 0.0f, 0.0f, 0.0f};
                    s_prevScale = {1.0f, 1.0f, 1.0f};
                }
            }
        }
    }
    ImGui::End();


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

    frameBuffer->mainViewFamily.directionalLight = state->directionalLight;
    frameBuffer->mainViewFamily.shadowConfig = state->shadowConfig;
    frameBuffer->mainViewFamily.postProcessConfig = state->postProcess;
    frameBuffer->mainViewFamily.gtaoConfig = state->gtaoConfig;
    frameBuffer->mainViewFamily.debugResourceName = state->debugResourceName;
    frameBuffer->mainViewFamily.debugTransformationType = state->debugTransformationType;
    frameBuffer->mainViewFamily.debugViewAspect = state->debugViewAspect;
}
}
