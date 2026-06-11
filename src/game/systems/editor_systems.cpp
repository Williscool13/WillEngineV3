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
#include "core/containers/arena_array.h"
#include "core/containers/arena_fixed_vector.h"
#include "engine/include/engine_context.h"
#include "core/input/input_frame.h"
#include "core/math/constants.h"
#include "engine/engine_api.h"
#include "engine/material_manager.h"
#include "engine/asset_manager.h"
#include "engine/core/model_id.h"
#include "engine/resources/texture/texture.h"
#include "game/fwd_components.h"
#include "game/components/common_components.h"
#include "game/components/editor_components.h"
#include "game/components/scene_components.h"

namespace Game
{
void MarkSceneModified(Engine::EngineState* state, StringID sceneId)
{
    if (!state->editor.modifiedScenes.Contains(sceneId)) {
        state->editor.modifiedScenes.PushBack(sceneId);
    }
}

void MarkEntitiesModified(Engine::EngineState* state, Core::Span<entt::entity> entities)
{
    for (auto e : entities) {
        if (auto* sc = state->registry.try_get<Component::SceneComponent>(e)) {
            MarkSceneModified(state, sc->sceneId);
        }
    }
}

void DrawMultiSelectEditor(Engine::EngineContext* ctx, Engine::EngineState* state, const Vec3& centroid, int transformCount)
{
    auto& entities = state->editor.selectedEntities;
    ImGui::Text("%zu entities selected", entities.Size());

    // Name
    if (ImGui::CollapsingHeader("Name##multi_name", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool allHaveName = true;
        bool allSame = true;
        const char* firstName = nullptr;
        for (auto e : entities) {
            auto* nc = state->registry.try_get<Component::NameComponent>(e);
            if (!nc) {
                allHaveName = false;
                break;
            }
            if (!firstName) {
                firstName = nc->name.c_str();
            }
            else if (strcmp(firstName, nc->name.c_str()) != 0) {
                allSame = false;
            }
        }

        if (allHaveName) {
            char buf[256];
            if (allSame) {
                strncpy_s(buf, firstName, sizeof(buf) - 1);
            }
            else {
                strncpy_s(buf, "...", sizeof(buf) - 1);
            }
            buf[sizeof(buf) - 1] = '\0';

            if (ImGui::InputText("Name##multi", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                for (auto e : entities) {
                    state->registry.get<Component::NameComponent>(e).name = Core::InlineString<256>(buf);
                }
                MarkEntitiesModified(state, entities);
            }
        }
        else {
            ImGui::TextDisabled("Not all entities have NameComponent");
        }
    }

    // Folder
    if (ImGui::CollapsingHeader("Folder##multi_folder", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool allHaveFolder = true;
        bool folder0Same = true, folder1Same = true;
        Core::ShortString firstFolder0, firstFolder1;
        StringID firstFolder0Id;
        bool first = true;

        for (auto e : entities) {
            auto* fc = state->registry.try_get<Component::EntityFolderComponent>(e);
            if (!fc) {
                allHaveFolder = false;
                break;
            }
            if (first) {
                firstFolder0 = fc->folderHierarchyNames[0];
                firstFolder1 = fc->folderHierarchyNames[1];
                firstFolder0Id = fc->folderHierarchy[0];
                first = false;
            }
            else {
                if (!(fc->folderHierarchyNames[0] == firstFolder0)) {
                    folder0Same = false;
                }
                if (!(fc->folderHierarchyNames[1] == firstFolder1)) {
                    folder1Same = false;
                }
            }
        }

        if (allHaveFolder) {
            Core::ArenaVector<Core::ShortString> existingFolders0{&ctx->editorArena.Get(), 16};
            auto folderView = state->registry.view<Component::EntityFolderComponent>();
            for (auto e : folderView) {
                auto& fc = folderView.get<Component::EntityFolderComponent>(e);
                if (fc.folderHierarchyNames[0].Size() == 0) {
                    continue;
                }
                bool dup = false;
                for (auto& ex : existingFolders0) {
                    if (ex == fc.folderHierarchyNames[0]) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    existingFolders0.PushBack(fc.folderHierarchyNames[0]);
                }
            }
            std::ranges::sort(existingFolders0);

            const char* folder0Display = folder0Same ? (firstFolder0.Size() > 0 ? firstFolder0.c_str() : "(None)") : "...";

            ImGui::Text("Folder");
            if (ImGui::BeginCombo("##multi_folder_0", folder0Display)) {
                if (ImGui::Selectable("(None)", folder0Same && firstFolder0.Size() == 0)) {
                    for (auto e : entities) {
                        auto& fc = state->registry.get<Component::EntityFolderComponent>(e);
                        fc.folderHierarchyNames[0] = Core::ShortString();
                        fc.folderHierarchy[0] = StringID();
                        fc.folderHierarchyNames[1] = Core::ShortString();
                        fc.folderHierarchy[1] = StringID();
                    }
                    MarkEntitiesModified(state, entities);
                }
                for (auto& fn : existingFolders0) {
                    bool selected = folder0Same && fn == firstFolder0;
                    if (ImGui::Selectable(fn.c_str(), selected)) {
                        StringID id(fn.c_str(), fn.Size());
                        for (auto e : entities) {
                            auto& fc = state->registry.get<Component::EntityFolderComponent>(e);
                            fc.folderHierarchyNames[0] = fn;
                            fc.folderHierarchy[0] = id;
                        }
                        MarkEntitiesModified(state, entities);
                    }
                }
                ImGui::EndCombo();
            }

            if (folder0Same && firstFolder0Id.IsValid()) {
                Core::ArenaVector<Core::ShortString> existingFolders1{&ctx->editorArena.Get(), 16};
                for (auto e : folderView) {
                    auto& fc = folderView.get<Component::EntityFolderComponent>(e);
                    if (fc.folderHierarchy[0] != firstFolder0Id) {
                        continue;
                    }
                    if (fc.folderHierarchyNames[1].Size() == 0) {
                        continue;
                    }
                    bool dup = false;
                    for (auto& ex : existingFolders1) {
                        if (ex == fc.folderHierarchyNames[1]) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) {
                        existingFolders1.PushBack(fc.folderHierarchyNames[1]);
                    }
                }
                std::ranges::sort(existingFolders1);

                const char* folder1Display = folder1Same ? (firstFolder1.Size() > 0 ? firstFolder1.c_str() : "(None)") : "...";

                ImGui::Text("Subfolder");
                if (ImGui::BeginCombo("##multi_folder_1", folder1Display)) {
                    if (ImGui::Selectable("(None)", folder1Same && firstFolder1.Size() == 0)) {
                        for (auto e : entities) {
                            auto& fc = state->registry.get<Component::EntityFolderComponent>(e);
                            fc.folderHierarchyNames[1] = Core::ShortString();
                            fc.folderHierarchy[1] = StringID();
                        }
                        MarkEntitiesModified(state, entities);
                    }
                    for (auto& fn : existingFolders1) {
                        bool selected = folder1Same && fn == firstFolder1;
                        if (ImGui::Selectable(fn.c_str(), selected)) {
                            StringID id(fn.c_str(), fn.Size());
                            for (auto e : entities) {
                                auto& fc = state->registry.get<Component::EntityFolderComponent>(e);
                                fc.folderHierarchyNames[1] = fn;
                                fc.folderHierarchy[1] = id;
                            }
                            MarkEntitiesModified(state, entities);
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
        else {
            ImGui::TextDisabled("Not all entities have EntityFolderComponent");
        }
    }

    // Transform
    if (transformCount > 0 && ImGui::CollapsingHeader("Transform##multi_transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool allSameRot = true, allSameScale = true;
        glm::quat firstRot{};
        glm::vec3 firstScale{};
        bool first = true;
        for (auto e : entities) {
            auto* tf = state->registry.try_get<Component::TransformComponent>(e);
            if (!tf) {
                continue;
            }
            if (first) {
                firstRot = tf->rotation;
                firstScale = tf->scale;
                first = false;
            }
            else {
                if (glm::dot(firstRot, tf->rotation) < 0.9999f) {
                    allSameRot = false;
                }
                if (glm::any(glm::epsilonNotEqual(firstScale, tf->scale, 1e-4f))) {
                    allSameScale = false;
                }
            }
        }

        // Translation (on the centroid)
        glm::vec3 editCentroid = centroid;
        if (ImGui::DragFloat3("Translation##multi", &editCentroid.x, 0.1f)) {
            glm::vec3 delta = editCentroid - centroid;
            for (auto e : entities) {
                if (auto* tf = state->registry.try_get<Component::TransformComponent>(e)) {
                    tf->translation += delta;
                    state->registry.emplace_or_replace<Component::DirtyTransformTag>(e);
                }
            }
            MarkEntitiesModified(state, entities);
        }

        // Rotation
        if (allSameRot) {
            glm::vec3 euler = glm::degrees(glm::eulerAngles(firstRot));
            if (ImGui::DragFloat3("Rotation##multi", &euler.x, 0.5f)) {
                glm::quat newRot = glm::quat(glm::radians(euler));
                for (auto e : entities) {
                    if (auto* tf = state->registry.try_get<Component::TransformComponent>(e)) {
                        tf->rotation = newRot;
                        state->registry.emplace_or_replace<Component::DirtyTransformTag>(e);
                    }
                }
                MarkEntitiesModified(state, entities);
            }
        }
        else {
            ImGui::BeginDisabled();
            glm::vec3 dummy{0};
            ImGui::DragFloat3("Rotation##multi", &dummy.x, 0.5f);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Mixed rotations");
            }
        }

        // Scale
        if (allSameScale) {
            glm::vec3 scale = firstScale;
            if (ImGui::DragFloat3("Scale##multi", &scale.x, 0.01f)) {
                for (auto e : entities) {
                    if (auto* tf = state->registry.try_get<Component::TransformComponent>(e)) {
                        tf->scale = scale;
                        state->registry.emplace_or_replace<Component::DirtyTransformTag>(e);
                    }
                }
                MarkEntitiesModified(state, entities);
            }
        }
        else {
            ImGui::BeginDisabled();
            glm::vec3 dummy{0};
            ImGui::DragFloat3("Scale##multi", &dummy.x, 0.01f);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Mixed scales");
            }
        }
    }
}

void EditorUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->editor.bAutoSave && !state->editor.modifiedScenes.IsEmpty()) {
        state->editor.autoSaveTimer += state->timeFrame->deltaTime;
        if (state->editor.autoSaveTimer >= state->editor.autoSaveInterval) {
            state->editor.autoSaveTimer = 0.0f;
            for (StringID sceneId : state->editor.modifiedScenes) {
                const auto& sceneCache = ctx->assetManager->GetSceneCache();
                if (const auto* it = sceneCache.Find(sceneId)) {
                    SaveSceneToFile(sceneId, it->sceneName.c_str(), state, ctx->assetManager, ctx);
                }
            }
            state->editor.modifiedScenes.Clear();
        }
    }

    bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (state->bIsPlaying) {
        if (!popupOpen && state->inputFrame->GetKey(Key::ESCAPE).pressed) {
            if (state->bGameCursorCaptured) {
                state->bGameCursorCaptured = false;
                ctx->setCursorHiddenFn(false);
            }
            else {
                PlayStop(ctx, state);
            }
        }

        if (!state->bGameCursorCaptured && !ctx->bImguiMouseCaptured
            && state->inputFrame->GetMouse(MouseButton::LMB).pressed) {
            state->bGameCursorCaptured = true;
            ctx->setCursorHiddenFn(true);
        }

        return;
    }

    if (ctx->bImGuiWantsTextInput) { return; }

    const bool ctrlHeld = state->inputFrame->GetKey(Key::LCTRL).down || state->inputFrame->GetKey(Key::RCTRL).down;

    const bool multiSelectActive = state->editor.selectedEntities.Size() > 1;
    if (multiSelectActive && state->editor.currentGizmoOperation == ImGuizmo::SCALE) {
        state->editor.currentGizmoOperation = ImGuizmo::TRANSLATE;
    }
    if (!ctrlHeld) {
        if (state->inputFrame->GetKey(Key::W).pressed) {
            state->editor.currentGizmoOperation = ImGuizmo::TRANSLATE;
        }
        else if (state->inputFrame->GetKey(Key::E).pressed) {
            state->editor.currentGizmoOperation = ImGuizmo::ROTATE;
        }
        else if (state->inputFrame->GetKey(Key::R).pressed && !multiSelectActive) {
            state->editor.currentGizmoOperation = ImGuizmo::SCALE;
        }
    }

    const bool rmbHeld = state->inputFrame->GetMouse(MouseButton::RMB).down;
    if (!rmbHeld) {
        if (!popupOpen && ctrlHeld && state->inputFrame->GetKey(Key::W).pressed) {
            state->editor.bWantCopyEntities = true;
        }

        if (!popupOpen && state->inputFrame->GetKey(Key::DEL).pressed) {
            state->editor.bWantDeleteEntities = true;
        }

        if (!popupOpen && state->inputFrame->GetKey(Key::ESCAPE).pressed) {
            state->editor.selectedEntities.Clear();
        }

        if (!popupOpen && state->inputFrame->GetKey(Key::F).pressed && !state->editor.selectedEntities.IsEmpty()) {
            entt::entity target = state->editor.selectedEntities.Front();
            if (state->registry.valid(target)) {
                auto* targetTransform = state->registry.try_get<Component::TransformComponent>(target);

                auto editorCamView = state->registry.view<Component::EditorCameraTag, Component::FreeCameraComponent, Component::TransformComponent>();
                for (entt::entity camEntity : editorCamView) {
                    auto& camTransform = editorCamView.get<Component::TransformComponent>(camEntity);
                    const glm::vec3 camForward = glm::normalize(camTransform.rotation * WORLD_FORWARD);

                    glm::vec3 focusPoint = targetTransform ? targetTransform->translation : glm::vec3(0.0f);
                    float focusDist = 1.0f;

                    Engine::StaticModelHandle modelHandle = Engine::StaticModelHandle::INVALID;
                    if (const auto* rt = state->registry.try_get<Component::MeshRuntime>(target)) {
                        modelHandle = rt->modelHandle;
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


    for (const auto& hotkey : DEBUG_HOTKEYS) {
        if (state->inputFrame->GetKey(hotkey.key).pressed) {
            if (state->debug.resourceName == hotkey.resourceName && state->debug.viewAspect == hotkey.aspect && state->debug.transformationType == hotkey.transform) {
                state->debug.resourceName.Clear();
            }
            else {
                state->debug.resourceName = Core::InlineString(hotkey.resourceName);
                state->debug.transformationType = hotkey.transform;
                state->debug.viewAspect = hotkey.aspect;
            }
        }
    }
}

void DrawEditorInterface(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    state->editor.texResidency.Tick(ctx);
    state->editor.ResetFrameCache();
    state->editor.bExclusiveGizmoActivePrev = state->editor.bExclusiveGizmoActive;
    state->editor.bExclusiveGizmoActive = false;


    bool bJustSelected = false;

    const bool ctrlHeld = state->inputFrame->GetKey(Key::LCTRL).down || state->inputFrame->GetKey(Key::RCTRL).down;
    if (!ctx->bImguiMouseCaptured && !state->editor.bExclusiveGizmoActivePrev && state->inputFrame->GetMouse(MouseButton::LMB).pressed) {
        auto it = state->stableIdToEntityMap.Find(StringID{ctx->lastKnownStableIdUnderCursor});
        if (it != nullptr) {
            bJustSelected = true;
            entt::entity clicked = *it;
            if (ctrlHeld) {
                auto pos = std::find(state->editor.selectedEntities.begin(), state->editor.selectedEntities.end(), clicked);
                if (pos != state->editor.selectedEntities.end()) {
                    state->editor.selectedEntities.Remove(pos);
                }
                else {
                    state->editor.selectedEntities.PushBack(clicked);
                }
            }
            else {
                state->editor.selectedEntities.Clear();
                state->editor.selectedEntities.PushBack(clicked);
            }
        }
        else if (!ctrlHeld) {
            state->editor.selectedEntities.Clear();
        }
    }


    if (state->editor.bWantDeleteEntities) {
        state->editor.bWantDeleteEntities = false;
        for (entt::entity entity : state->editor.selectedEntities) {
            if (!state->registry.valid(entity)) continue;
            state->registry.destroy(entity);
        }
        state->editor.selectedEntities.Clear();
    }

    if (state->editor.bWantCopyEntities) {
        state->editor.bWantCopyEntities = false;
        auto copies = Core::ArenaFixedVector<entt::entity>(&ctx->editorArena.Get(), state->editor.selectedEntities.Size());
        for (entt::entity entity : state->editor.selectedEntities) {
            if (!state->registry.valid(entity)) continue;
            copies.PushBack(CopySceneEntity(state, entity, state->currentSceneId));
        }
        state->editor.selectedEntities.Clear();
        for (auto copy : copies) {
            state->editor.selectedEntities.PushBack(copy);
        }
    }

    if (ImGui::Begin("Debug View")) {
        bool bProjectConfigChanged = false;

        ImGui::SeparatorText("Project Config");
        bool& bAutoSave = state->projectConfig.bAutoSave;
        ImGui::BeginDisabled(bAutoSave);
        if (ImGui::Button("Save Config")) {
            state->projectConfig.restir = state->debug.restir;
            state->projectConfig.relax = state->debug.relax;
            Engine::WriteProjectConfig(state->projectConfig);
        }
        ImGui::EndDisabled();
        if (bAutoSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Auto-save is enabled");
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto-save", &bAutoSave)) {
            Engine::WriteProjectConfig(state->projectConfig);
        }

        ImGui::Separator();

        ImGui::Checkbox("Enable UI", &state->debug.bEnableUI);
        ImGui::Checkbox("Wireframe", &state->debug.bWireframe);
        const char* lightingModeLabels[] = {"Default", "ReSTIR", "Ground-Truth ReSTIR", "Path Tracing"};
        Core::LightingMode prevLightingMode = state->lighting.lightingMode;
        int32_t lightingModeIndex = static_cast<int32_t>(state->lighting.lightingMode);
        if (ImGui::Combo("Lighting Mode", &lightingModeIndex, lightingModeLabels, 4)) {
            state->lighting.lightingMode = static_cast<Core::LightingMode>(lightingModeIndex);
            bProjectConfigChanged = true;

            if (prevLightingMode != Core::LightingMode::GroundTruthReSTIR && state->lighting.lightingMode == Core::LightingMode::GroundTruthReSTIR) {
                state->lighting.bResetGroundTruth = true;
            }
        }

        ImGui::Separator();

        auto bIsGroundTruth = state->lighting.lightingMode == Core::LightingMode::GroundTruthReSTIR;
        ImGui::BeginDisabled(bIsGroundTruth);
        // Shading Pipeline Overrides
        {
            Core::Span<const StringID> shadingPipelines = ctx->pipelineManager->GetShadingPipelines();
            const int32_t pipelineCount = static_cast<int32_t>(shadingPipelines.Size());
            Core::Arena& arena = ctx->editorArena.Get();

            int currentShader = pipelineCount; // "None"
            for (int32_t i = 0; i < pipelineCount; ++i) {
                if (state->debug.shadingShaderOverride == shadingPipelines[i]) {
                    currentShader = i;
                    break;
                }
            }

            Core::ArenaArray<Core::InlineString<> > labels(&arena, pipelineCount + 1);
            labels[0] = Core::InlineString("None");
            for (int32_t i = 0; i < pipelineCount; ++i) { labels[i + 1] = Core::InlineString(shadingPipelines[i].ToString()); }
            const int comboIndex = currentShader == pipelineCount ? 0 : currentShader + 1;
            int selected = comboIndex;
            auto getter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<> >*>(data))[idx].c_str(); };
            if (ImGui::Combo("Shading Override", &selected, getter, &labels, static_cast<int32_t>(labels.Size()))) {
                state->debug.shadingShaderOverride = selected == 0 ? StringID{} : shadingPipelines[selected - 1];
            }
        }
        // Lighting Pipeline Overrides
        {
            Core::Span<const StringID> lightingPipelines = ctx->pipelineManager->GetLightingPipelines();
            const int32_t pipelineCount = static_cast<int32_t>(lightingPipelines.Size());
            Core::Arena& arena = ctx->editorArena.Get();

            int currentShader = pipelineCount; // "None"
            for (int32_t i = 0; i < pipelineCount; ++i) {
                if (state->debug.lightingShaderOverride == lightingPipelines[i]) {
                    currentShader = i;
                    break;
                }
            }

            Core::ArenaArray<Core::InlineString<> > labels(&arena, pipelineCount + 1);
            labels[0] = Core::InlineString("None");
            for (int32_t i = 0; i < pipelineCount; ++i) { labels[i + 1] = Core::InlineString(lightingPipelines[i].ToString()); }
            const int comboIndex = currentShader == pipelineCount ? 0 : currentShader + 1;
            int selected = comboIndex;
            auto getter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<> >*>(data))[idx].c_str(); };
            if (ImGui::Combo("Lighting Override", &selected, getter, &labels, static_cast<int32_t>(labels.Size()))) {
                state->debug.lightingShaderOverride = selected == 0 ? StringID{} : lightingPipelines[selected - 1];
            }
        }
        ImGui::EndDisabled();
        ImGui::Separator();

        ImGui::Text("Current Debug View: %s", state->debug.resourceName.IsEmpty() ? "None" : state->debug.resourceName.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Disable Debug View")) {
            state->debug.resourceName.Clear();
        }
        ImGui::Checkbox("Enable V-Buffer Shade Dispatch Bucketing Visualization", &state->debug.bEnableShadeDispatchBucketingVisualization);
        ImGui::Checkbox("Enable V-Buffer Lighting Bucketing Visualization", &state->debug.bEnableLightingBucketingVisualization);

        ImGui::Separator();
        if (ImGui::CollapsingHeader("ReSTIR DI Settings")) {
            Core::ReSTIRParams& restir = state->debug.restir;
            ImGui::Separator();
            if (ImGui::Checkbox("Half Res", &restir.bHalfRes)) {
                bProjectConfigChanged = true;
            }
            ImGui::Separator();
            int spatialRadius = static_cast<int>(restir.spatialRadius);
            if (ImGui::SliderInt("Spatial Radius", &spatialRadius, 1, 100)) {
                restir.spatialRadius = static_cast<uint32_t>(spatialRadius);
                bProjectConfigChanged = true;
            }
            int spatialNeighbors = static_cast<int>(restir.spatialNeighbors);
            if (ImGui::SliderInt("Spatial Neighbors", &spatialNeighbors, 1, 16)) {
                restir.spatialNeighbors = static_cast<uint32_t>(spatialNeighbors);
                bProjectConfigChanged = true;
            }
            int spatialMCap = static_cast<int>(restir.spatialMCap);
            if (ImGui::SliderInt("Spatial M Cap", &spatialMCap, 1, 2000)) {
                restir.spatialMCap = static_cast<uint32_t>(spatialMCap);
                bProjectConfigChanged = true;
            }
            int temporalMCap = static_cast<int>(restir.temporalMCap);
            if (ImGui::SliderInt("Temporal M Cap", &temporalMCap, 1, 2000)) {
                restir.temporalMCap = static_cast<uint32_t>(temporalMCap);
                bProjectConfigChanged = true;
            }
            ImGui::Separator();
            if (ImGui::SliderFloat("IBL Intensity##restir", &restir.iblIntensity, 0.0f, 2.0f)) {
                bProjectConfigChanged = true;
            }
            ImGui::Separator();
            const char* modeLabels[] = {"Main + Temporal + 1x Spatial", "Combined + 1x Spatial"};
            int modeIdx = static_cast<int>(restir.mode);
            if (ImGui::Combo("ReSTIR Mode", &modeIdx, modeLabels, 2)) {
                restir.mode = static_cast<Core::ReSTIRParams::Mode>(modeIdx);
                bProjectConfigChanged = true;
            }
            if (ImGui::Checkbox("Spatial 2", &restir.bSpatial2)) {
                bProjectConfigChanged = true;
            }
            ImGui::Separator();
            const char* stopLabels[] = {"After Spatial 1", "After Temporal", "After Generate"};
            int stopIdx = static_cast<int>(restir.debugStop);
            if (ImGui::Combo("Debug Stop", &stopIdx, stopLabels, 3)) {
                restir.debugStop = static_cast<Core::ReSTIRDebugStop>(stopIdx);
                bProjectConfigChanged = true;
            }
        }

        ImGui::BeginDisabled(true);
        ImGui::Checkbox("Enable Portals", &state->debug.bEnablePortal);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Portals are not currently functional");
        }
        ImGui::EndDisabled();


        ImGui::Separator();

        if (ImGui::CollapsingHeader("Hotkeys", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* keyNames[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
            for (size_t i = 0; i < std::size(DEBUG_HOTKEYS); ++i) {
                ImGui::Text("%s: %s (%s)", keyNames[i], DEBUG_HOTKEYS[i].name, DEBUG_HOTKEYS[i].resourceName);
            }
        }

        ImGui::Separator();

        auto setDebugTarget = [&](const char* name, DebugTransformationType _transform, Core::DebugViewAspect aspect) {
            if (state->debug.resourceName == name && state->debug.viewAspect == aspect && state->debug.transformationType == _transform) {
                state->debug.resourceName.Clear();
            }
            else {
                state->debug.resourceName = Core::InlineString(name);
                state->debug.transformationType = _transform;
                state->debug.viewAspect = aspect;
            }
        };

        if (ImGui::CollapsingHeader("Visibility Buffer")) {
            if (ImGui::Button("Visibility Buffer (Instance)")) setDebugTarget("visibility_target", DebugTransformationType::VisBuffInstance, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Buffer (Meshlet)")) setDebugTarget("visibility_target", DebugTransformationType::VisBuffMeshlet, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Buffer (Triangle)")) setDebugTarget("visibility_target", DebugTransformationType::VisBuffTriangle, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Barycentric")) setDebugTarget("visibility_barycentric", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Derivatives")) setDebugTarget("visibility_derivatives", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Bucketing (Shading)")) setDebugTarget("visibility_target", DebugTransformationType::VisBucketShading, Core::DebugViewAspect::None);
            if (ImGui::Button("Visibility Bucketing (Lighting)")) setDebugTarget("visibility_target", DebugTransformationType::VisBucketLighting, Core::DebugViewAspect::None);
        }
        if (ImGui::CollapsingHeader("ReSTIR DI Visualize")) {
            if (ImGui::Button("Generate Light Index")) setDebugTarget("depth_target", DebugTransformationType::ReservoirLightIdx, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Generate W")) setDebugTarget("depth_target", DebugTransformationType::ReservoirGenerateW, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Temporal Light Index")) setDebugTarget("depth_target", DebugTransformationType::ReservoirTemporalLightIdx, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Temporal W")) setDebugTarget("depth_target", DebugTransformationType::ReservoirTemporalW, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Spatial Light Index")) setDebugTarget("depth_target", DebugTransformationType::ReservoirSpatialLightIdx, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Spatial W")) setDebugTarget("depth_target", DebugTransformationType::ReservoirSpatialW, Core::DebugViewAspect::Depth);
            if (ImGui::Button("History Light Index")) setDebugTarget("depth_target", DebugTransformationType::ReservoirHistoryLightIdx, Core::DebugViewAspect::Depth);
            if (ImGui::Button("History W")) setDebugTarget("depth_target", DebugTransformationType::ReservoirHistoryW, Core::DebugViewAspect::Depth);
        }
        if (ImGui::CollapsingHeader("G-Buffer")) {
            if (ImGui::Button("Depth")) setDebugTarget("depth_target", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            ImGui::SameLine();
            if (ImGui::Button("Stencil")) setDebugTarget("depth_target", DebugTransformationType::StencilRemap, Core::DebugViewAspect::Stencil);

            if (ImGui::Button("Albedo")) setDebugTarget("gbuffer_two", DebugTransformationType::GBufferAlbedo, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Normal")) setDebugTarget("gbuffer_one", DebugTransformationType::GBufferNormal, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("PBR")) setDebugTarget("gbuffer_one", DebugTransformationType::GBufferPBR, Core::DebugViewAspect::None);

            if (ImGui::Button("Emissive")) setDebugTarget("gbuffer_two", DebugTransformationType::GBufferEmissive, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Motion Vectors")) setDebugTarget("gbuffer_one", DebugTransformationType::GBufferMotionVectors, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("View Z Delta")) setDebugTarget("gbuffer_one", DebugTransformationType::GBufferViewZDelta, Core::DebugViewAspect::None);

            if (ImGui::Button("Intermediate One (Diffuse)")) setDebugTarget("intermediate_one", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Intermediate Two (Specular)")) setDebugTarget("intermediate_two", DebugTransformationType::None, Core::DebugViewAspect::None);

            if (ImGui::Button("View Space Position")) setDebugTarget("depth_target", DebugTransformationType::ViewSpacePosition, Core::DebugViewAspect::Depth);
            ImGui::SameLine();
            if (ImGui::Button("NdotV")) setDebugTarget("gbuffer_one", DebugTransformationType::NdotV, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Shadows")) {
            if (ImGui::Button("Shadow Cascade 0")) setDebugTarget("shadow_cascade_0", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadow Cascade 1")) setDebugTarget("shadow_cascade_1", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadow Cascade 2")) setDebugTarget("shadow_cascade_2", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadow Cascade 3")) setDebugTarget("shadow_cascade_3", DebugTransformationType::DepthRemap, Core::DebugViewAspect::Depth);
            if (ImGui::Button("Shadows Resolve")) setDebugTarget("shadows_resolve_target", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Lighting")) {
            if (ImGui::Button("Shading Output")) setDebugTarget("shading_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Depth")) setDebugTarget("gtao_depth", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO AO")) setDebugTarget("gtao_ao", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Edges")) setDebugTarget("gtao_edges", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("GTAO Filtered")) setDebugTarget("gtao_filtered", DebugTransformationType::None, Core::DebugViewAspect::None);
        }

        if (ImGui::CollapsingHeader("Anti-Aliasing")) {
            if (ImGui::Button("TAA Current")) setDebugTarget("taa_current", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("TAA Output")) setDebugTarget("taa_output", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::Separator();
            if (ImGui::Button("SMAA Edges")) setDebugTarget("smaa_edges", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("SMAA Blend Weights")) setDebugTarget("smaa_blend", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("SMAA Output")) setDebugTarget("smaa_output", DebugTransformationType::None, Core::DebugViewAspect::None);
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

        if (ImGui::CollapsingHeader("RELAX Denoiser")) {
            // Tiles
            if (ImGui::Button("Tiles")) setDebugTarget("relax_tiles", DebugTransformationType::None, Core::DebugViewAspect::None);
            // Prepass
            if (ImGui::Button("Spec Prepass")) setDebugTarget("relax_spec_prepass", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff Prepass")) setDebugTarget("relax_diff_prepass", DebugTransformationType::None, Core::DebugViewAspect::None);

            if (ImGui::Button("Spec Illum")) setDebugTarget("relax_spec_illum", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff Illum")) setDebugTarget("relax_diff_illum", DebugTransformationType::None, Core::DebugViewAspect::None);

            if (ImGui::Button("Spec Illum Hist")) setDebugTarget("relax_spec_illum_history", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff Illum Hist")) setDebugTarget("relax_diff_illum_history", DebugTransformationType::None, Core::DebugViewAspect::None);

            if (ImGui::Button("Spec Fast")) setDebugTarget("relax_spec_fast", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff Fast")) setDebugTarget("relax_diff_fast", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("History Length")) setDebugTarget("relax_history_length", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Spec Hit Dist")) setDebugTarget("relax_spec_hit_dist", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Reproj Confidence")) setDebugTarget("relax_spec_reproj_confidence", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("Prev NR")) setDebugTarget("relax_prev_nr", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("ATrous Spec 0")) setDebugTarget("relax_atrous_spec_0", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Spec History")) setDebugTarget("relax_spec_hist", DebugTransformationType::None, Core::DebugViewAspect::None);
            if (ImGui::Button("ATrous Diff 0")) setDebugTarget("relax_atrous_diff_0", DebugTransformationType::None, Core::DebugViewAspect::None);
            ImGui::SameLine();
            if (ImGui::Button("Diff History")) setDebugTarget("relax_diff_hist", DebugTransformationType::None, Core::DebugViewAspect::None);
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

        if (bProjectConfigChanged && state->projectConfig.bAutoSave) {
            state->projectConfig.restir = state->debug.restir;
            state->projectConfig.relax = state->debug.relax;
            state->projectConfig.lightingMode = state->lighting.lightingMode;
            Engine::WriteProjectConfig(state->projectConfig);
        }
    }
    ImGui::End();

    if (ImGui::Begin("Gameplay")) {
        if (state->bIsPlaying) {
            ImGui::Text("Checkpoint ID:       %llu", state->currentCheckpointId.id);
            ImGui::Text("Checkpoint Priority: %d", state->currentCheckpointPriority);
        }
        else {
            ImGui::TextDisabled("Not playing");
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

        if (!state->editor.selectedEntities.IsEmpty()) {
            entt::entity target = state->editor.selectedEntities.Front();
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

    const bool multiSelected = state->editor.selectedEntities.Size() > 1;
    if (multiSelected) {
        state->editor.currentGizmoMode = ImGuizmo::WORLD;
    }

    if (ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (ImGui::RadioButton("T##gizmo_op", state->editor.currentGizmoOperation == ImGuizmo::TRANSLATE)) { state->editor.currentGizmoOperation = ImGuizmo::TRANSLATE; }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Translate (W)"); }
        ImGui::SameLine();
        if (ImGui::RadioButton("R##gizmo_op", state->editor.currentGizmoOperation == ImGuizmo::ROTATE)) { state->editor.currentGizmoOperation = ImGuizmo::ROTATE; }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Rotate (E)"); }
        ImGui::SameLine();
        ImGui::BeginDisabled(multiSelected);
        if (ImGui::RadioButton("S##gizmo_op", state->editor.currentGizmoOperation == ImGuizmo::SCALE)) { state->editor.currentGizmoOperation = ImGuizmo::SCALE; }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(multiSelected ? "Scale (R) — unavailable for multi-selection" : "Scale (R)");
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        ImGui::BeginDisabled(state->editor.currentGizmoOperation == ImGuizmo::SCALE || multiSelected);
        if (ImGui::RadioButton("L##gizmo_mode", state->editor.currentGizmoMode == ImGuizmo::LOCAL)) { state->editor.currentGizmoMode = ImGuizmo::LOCAL; }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("Local space"); }
        ImGui::SameLine();
        if (ImGui::RadioButton("W##gizmo_mode", state->editor.currentGizmoMode == ImGuizmo::WORLD)) { state->editor.currentGizmoMode = ImGuizmo::WORLD; }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("World space"); }
        ImGui::EndDisabled();

        if (state->editor.currentGizmoOperation == ImGuizmo::SCALE) {
            ImGui::SameLine();
            ImGui::Checkbox("Uni##gizmo_uni", &state->editor.bUniformScaleMode);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Uniform scale"); }
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        ImGui::Checkbox("Snap##gizmo_snap", &state->editor.bSnapEnabled);
        if (state->editor.bSnapEnabled) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(55.0f);
            if (state->editor.currentGizmoOperation == ImGuizmo::TRANSLATE) {
                ImGui::DragFloat("##snap_val", &state->editor.snapTranslation, 0.05f, 0.01f, 10.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translation snap (world units)");
            }
            else if (state->editor.currentGizmoOperation == ImGuizmo::ROTATE) {
                ImGui::DragFloat("##snap_val", &state->editor.snapRotation, 1.0f, 1.0f, 180.0f, "%.0f deg");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotation snap (degrees)");
            }
            else {
                ImGui::DragFloat("##snap_val", &state->editor.snapScale, 0.05f, 0.01f, 2.0f, "%.2f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale snap");
            }
            if (state->editor.currentGizmoOperation == ImGuizmo::TRANSLATE) {
                ImGui::SameLine();
                // todo remove, just infer from local/world space
                ImGui::Checkbox("World Grid##snap_world_grid", &state->editor.bSnapWorldGrid);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap to world-origin-aligned grid rather than drag-relative increments");
            }
        }

        if (state->editor.prevSelectedEntities != state->editor.selectedEntities) {
            ImGui::ClearActiveID();
            if (state->editor.selectedEntities.Size() == 1) {
                if (auto* tf = state->registry.try_get<Component::TransformComponent>(state->editor.selectedEntities[0])) {
                    const bool scaleIsUniform = glm::epsilonEqual(tf->scale.x, tf->scale.y, 1e-5f) && glm::epsilonEqual(tf->scale.y, tf->scale.z, 1e-5f);
                    state->editor.bUniformScaleMode = scaleIsUniform;
                }
            }
        }
        state->editor.prevSelectedEntities = state->editor.selectedEntities;

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

        // Right-aligned controls: sprite checkbox + light debug combo + physics debug combo
        {
            static constexpr const char* kLightDebugLabels[] = {"None", "Selected", "All"};
            static constexpr const char* kPhysicsDebugLabels[] = {"Off", "Sensor Only", "Sensor + Tag", "On"};
            int lightMode = static_cast<int>(state->editor.lightDebugDrawMode);
            int physicsMode = static_cast<int>(state->editor.physicsDebugMode);
            constexpr float checkW = 16.0f;
            constexpr float lightComboW = 72.0f;
            constexpr float physicsComboW = 110.0f;
            constexpr float spacing = 4.0f;
            const float totalW = checkW + spacing + lightComboW + spacing + physicsComboW;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - totalW);
            ImGui::Checkbox("##light_sprites", &state->editor.bShowLightSprites);
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Show light sprite icons in viewport"); }
            ImGui::SameLine(0.0f, spacing);
            ImGui::SetNextItemWidth(lightComboW);
            if (ImGui::Combo("##light_debug", &lightMode, kLightDebugLabels, IM_ARRAYSIZE(kLightDebugLabels))) {
                state->editor.lightDebugDrawMode = static_cast<Engine::LightDebugDrawMode>(lightMode);
            }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Light debug draws: None / Selected entity only / All lights in scene"); }
            ImGui::SameLine(0.0f, spacing);
            ImGui::SetNextItemWidth(physicsComboW);
            if (ImGui::Combo("##physics_debug", &physicsMode, kPhysicsDebugLabels, IM_ARRAYSIZE(kPhysicsDebugLabels))) {
                state->editor.physicsDebugMode = static_cast<Engine::PhysicsDebugMode>(physicsMode);
            }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Physics debug draw: Off / Sensor Only / Sensor + Tag / On (all)"); }
        }
    }
    ImGui::End();

    if (ImGui::Begin("Scene Browser")) {
        const auto& sceneCache = ctx->assetManager->GetSceneCache();

        if (!sceneCache.IsEmpty() && !sceneCache.Contains(state->currentSceneId)) {
            for (const auto& [id, meta] : sceneCache) {
                state->currentSceneId = id;
                state->currentSceneName = meta.sceneName;
                break;
            }
        }
        if (sceneCache.IsEmpty()) {
            state->currentSceneId = {};
            state->currentSceneName.Clear();
        }

        const bool bIsLoaded = std::ranges::any_of(state->editor.loadedScenes, [&](const auto& m) { return m.sceneId == state->currentSceneId; });
        const bool bIsModified = std::ranges::find(state->editor.modifiedScenes, state->currentSceneId) != state->editor.modifiedScenes.end();
        const bool bIsMaxLoaded = state->editor.loadedScenes.Size() > Engine::MAX_LOADED_SCENES;
        const bool hasScene = sceneCache.Contains(state->currentSceneId);

        // Scene dropdown
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##scene_list", state->currentSceneName.c_str())) {
            struct ScenePair
            {
                StringID sceneId;
                Core::InlineString<128> name;
            };
            auto sceneList = Core::ArenaFixedVector<ScenePair>(&ctx->editorArena.Get(), sceneCache.Size());
            for (const auto& [id, meta] : sceneCache) {
                sceneList.EmplaceBack(id, meta.sceneName);
            }
            std::ranges::sort(sceneList, {}, &ScenePair::name);

            for (auto& [id, name] : sceneList) {
                const bool selected = (id == state->currentSceneId);
                if (ImGui::Selectable(name.c_str(), selected)) {
                    state->currentSceneId = id;
                    state->currentSceneName = name;
                }
                if (std::ranges::any_of(state->editor.loadedScenes, [&](const auto& m) { return m.sceneId == id; })) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(loaded)");
                }
            }
            ImGui::EndCombo();
        }

        ImGui::BeginDisabled(!hasScene || bIsLoaded || bIsMaxLoaded);
        if (ImGui::Button("Load")) {
            LoadSceneFromFile(state, ctx->assetManager, state->currentSceneId);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!bIsLoaded);
        if (ImGui::Button("Unload")) { UnloadScene(state, state->currentSceneId); }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!bIsLoaded);
        if (bIsModified) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 1.0f)); }
        if (ImGui::Button(bIsModified ? "Save*" : "Save")) {
            SaveSceneToFile(state->currentSceneId, state->currentSceneName.View(), state, ctx->assetManager, ctx);
            state->editor.modifiedScenes.RemoveFirst(state->currentSceneId);
        }
        if (bIsModified) { ImGui::PopStyleColor(); }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Checkbox("Auto", &state->editor.bAutoSave)) {
            state->editor.autoSaveTimer = 0.0f;
        }
        if (state->editor.bAutoSave && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Auto-save in %.0fs", state->editor.autoSaveInterval - state->editor.autoSaveTimer);
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasScene || bIsLoaded);
        if (ImGui::Button("Delete")) {
            ctx->assetManager->DeleteScene(state->currentSceneId);
            state->currentSceneId = {};
            state->currentSceneName.Clear();
        }
        ImGui::EndDisabled();
        if (bIsLoaded && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Unload scene before deleting");
        }

        ImGui::TextDisabled("ID: %llu", state->currentSceneId.id);

        ImGui::BeginDisabled(!hasScene);
        if (ImGui::Button("Set Default")) {
            state->projectConfig.defaultScene = Core::InlineString<256>(state->currentSceneName.View());
            Engine::WriteProjectConfig(state->projectConfig);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && hasScene) {
            ImGui::SetTooltip("Set '%s' as the scene loaded on startup (non-editor)", state->currentSceneName.c_str());
        }

        ImGui::SeparatorText("New Scene");
        static char newSceneName[128] = "New Scene";
        ImGui::InputText("##new_scene_name", newSceneName, sizeof(newSceneName));
        ImGui::SameLine();
        const bool nameEmpty = newSceneName[0] == '\0';
        bool nameInUse = false;
        if (!nameEmpty) {
            for (const auto& pair : sceneCache) {
                if (pair.value.sceneName == newSceneName) {
                    nameInUse = true;
                    break;
                }
            }
        }
        ImGui::BeginDisabled(nameEmpty || nameInUse);
        if (ImGui::Button("Create")) {
            StringID newId{state->rng()};
            ctx->assetManager->RegisterScene(newId, newSceneName);
            state->currentSceneId = newId;
            state->currentSceneName = Core::InlineString<128>(newSceneName);
            state->editor.loadedScenes.PushBack({newId, 100});
            state->editor.modifiedScenes.PushBack(newId);
            newSceneName[0] = '\0';
        }
        ImGui::EndDisabled();
        if (nameInUse && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("A scene with this name already exists");
        }

        ImGui::SeparatorText("Spawn Model");

        const auto& modelCache = ctx->assetManager->GetModelCache();
        static int selectedModel = 0;

        struct ModelPair
        {
            Core::InlineString<128> name;
            Engine::ModelID id;
        };

        if (modelCache.IsEmpty()) { ImGui::TextDisabled("No models loaded"); }
        auto modelList = Core::ArenaFixedVector<ModelPair>(&ctx->editorArena.Get(), std::max(modelCache.Size(), size_t{1}));
        for (const auto& [id, meta] : modelCache) {
            modelList.EmplaceBack(meta.name, id);
        }

        if (!modelList.IsEmpty()) {
            std::ranges::sort(modelList, {}, &ModelPair::name);
            selectedModel = std::clamp(selectedModel, 0, static_cast<int>(modelList.Size()) - 1);
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(modelList.IsEmpty());
        if (ImGui::BeginCombo("##model_list", modelList.IsEmpty() ? "No models" : modelList[selectedModel].name.c_str())) {
            for (int i = 0; i < static_cast<int>(modelList.Size()); ++i) {
                bool sel = (i == selectedModel);
                if (ImGui::Selectable(modelList[i].name.c_str(), sel)) {
                    selectedModel = i;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(modelList.IsEmpty());
        if (ImGui::Button("Spawn")) {
            glm::vec3 offset = cameraPos + normalize(cameraFwd) * 5.0f;
            auto spawned = SpawnModel(ctx, state, modelList[selectedModel].id, offset);
            if (!spawned.IsEmpty()) {
                state->editor.selectedEntities.Clear();
                for (auto entity : spawned) {
                    state->editor.selectedEntities.PushBack(entity);
                }
                MarkSceneModified(state, state->currentSceneId);
            }
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText("Prefabs");

        const bool hasOneSelected = state->editor.selectedEntities.Size() == 1;
        static char prefabName[128] = "New Prefab";

        Component::PrefabInstanceComponent* prefabInst = hasOneSelected ? state->registry.try_get<Component::PrefabInstanceComponent>(state->editor.selectedEntities[0]) : nullptr;
        const bool isExistingPrefab = prefabInst != nullptr;

        const bool isMasterPrefab = isExistingPrefab && prefabInst->bMasterPrefab;

        if (isExistingPrefab) {
            const auto* meta = ctx->assetManager->GetPrefabMetadata(prefabInst->prefabId);
            if (meta) {
                strncpy_s(prefabName, meta->prefabName.c_str(), sizeof(prefabName) - 1);
            }
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(!hasOneSelected);
        ImGui::BeginDisabled(isExistingPrefab);
        ImGui::InputText("##prefab_name", prefabName, sizeof(prefabName));
        ImGui::EndDisabled();
        ImGui::BeginDisabled(isExistingPrefab && !isMasterPrefab);
        if (ImGui::Button(isExistingPrefab ? "Save Prefab" : "Save as Prefab")) {
            SaveEntityAsPrefab(state, ctx->assetManager, ctx, state->editor.selectedEntities[0], prefabName);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        const auto& prefabCache = ctx->assetManager->GetPrefabCache();
        static int selectedPrefab = 0;
        struct PrefabPair
        {
            Core::InlineString<128> name;
            StringID id;
        };

        auto prefabList = Core::ArenaFixedVector<PrefabPair>(&ctx->editorArena.Get(), prefabCache.Size());
        for (const auto& [id, meta] : prefabCache) {
            prefabList.EmplaceBack(meta.prefabName, id);
        }

        if (!prefabList.IsEmpty()) {
            std::ranges::sort(prefabList, {}, &PrefabPair::name);
            selectedPrefab = std::clamp(selectedPrefab, 0, static_cast<int>(prefabList.Size()) - 1);
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(prefabList.IsEmpty());
        if (ImGui::BeginCombo("##prefab_list", prefabList.IsEmpty() ? "No prefabs" : prefabList[selectedPrefab].name.c_str())) {
            for (int i = 0; i < static_cast<int>(prefabList.Size()); ++i) {
                bool sel = (i == selectedPrefab);
                if (ImGui::Selectable(prefabList[i].name.c_str(), sel)) {
                    selectedPrefab = i;
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Spawn Prefab")) {
            const auto& viewData = frameBuffer->mainViewFamily.mainView.currentViewData;
            glm::vec3 spawnPos = viewData.cameraPos + viewData.cameraForward * 5.0f;
            entt::entity spawned = SpawnPrefab(state, ctx->assetManager, prefabList[selectedPrefab].id, spawnPos);
            if (spawned != entt::null) {
                state->editor.selectedEntities.Clear();
                state->editor.selectedEntities.PushBack(spawned);
                MarkSceneModified(state, state->currentSceneId);
            }
        }
        ImGui::SameLine(); {
            const StringID selectedPrefabId = prefabList.IsEmpty() ? StringID{} : prefabList[selectedPrefab].id;
            bool prefabInUse = false;
            if (!prefabList.IsEmpty()) {
                auto prefabView = state->registry.view<Component::PrefabInstanceComponent>();
                for (auto entity : prefabView) {
                    if (prefabView.get<Component::PrefabInstanceComponent>(entity).prefabId == selectedPrefabId) {
                        prefabInUse = true;
                        break;
                    }
                }
            }
            ImGui::BeginDisabled(prefabList.IsEmpty() || prefabInUse);
            if (ImGui::Button("Delete Prefab")) {
                ctx->assetManager->DeletePrefab(selectedPrefabId);
                selectedPrefab = 0;
            }
            ImGui::EndDisabled();
            if (prefabInUse && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Prefab is referenced by scene entities");
            }
        }
        ImGui::EndDisabled();

        ImGui::NewLine();

        ImGui::SeparatorText("Entities");
        if (ImGui::Button("Create Entity")) {
            auto newEntity = CreateSceneEntity(state);
            const auto& viewData = frameBuffer->mainViewFamily.mainView.currentViewData;
            state->registry.get<Component::TransformComponent>(newEntity).translation = viewData.cameraPos + viewData.cameraForward * 5.0f;
            state->editor.selectedEntities.Clear();
            state->editor.selectedEntities.PushBack(newEntity);
            MarkSceneModified(state, state->currentSceneId);
        }
        static char search[64] = {};
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##search", search, sizeof(search));

        entt::entity entityToDelete = entt::null;

        // Collect entities for the current scene, filtered by search
        struct EntityEntry
        {
            entt::entity entity;
            const char* label;
            uint64_t stableId;
            uint64_t sortOrder;
            StringID folder0;
            StringID folder1;
            const char* folderName0;
            const char* folderName1;
        };

        Core::ArenaVector<EntityEntry> entries{&ctx->editorArena.Get(), 1024};

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
            uint64_t sortOrder = stable ? stable->sortOrder : 0;

            StringID f0, f1;
            const char* fn0 = "";
            const char* fn1 = "";
            if (auto* fc = state->registry.try_get<Component::EntityFolderComponent>(entity)) {
                f0 = fc->folderHierarchy[0];
                f1 = fc->folderHierarchy[1];
                fn0 = fc->folderHierarchyNames[0].c_str();
                fn1 = fc->folderHierarchyNames[1].c_str();
            }
            entries.PushBack({entity, label, stableId, sortOrder, f0, f1, fn0, fn1});
        }
        std::ranges::sort(entries, [](const EntityEntry& a, const EntityEntry& b) { return a.sortOrder < b.sortOrder; });

        // Draw a single entity row. prev/next are neighbors within the same group for reordering.
        auto drawEntityRow = [&](const EntityEntry& e, const EntityEntry* prev, const EntityEntry* next) {
            ImGui::BeginDisabled(prev == nullptr);
            if (ImGui::SmallButton(fmt::format("^##{}", e.stableId).c_str())) {
                std::swap(state->registry.get<Component::StableIdComponent>(e.entity).sortOrder,
                          state->registry.get<Component::StableIdComponent>(prev->entity).sortOrder);
                MarkSceneModified(state, state->currentSceneId);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(next == nullptr);
            if (ImGui::SmallButton(fmt::format("v##{}", e.stableId).c_str())) {
                std::swap(state->registry.get<Component::StableIdComponent>(e.entity).sortOrder,
                          state->registry.get<Component::StableIdComponent>(next->entity).sortOrder);
                MarkSceneModified(state, state->currentSceneId);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton(fmt::format("X##{}", e.stableId).c_str())) {
                entityToDelete = e.entity;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(fmt::format("C##{}", e.stableId).c_str())) {
                entt::entity copied = CopySceneEntity(state, e.entity, state->currentSceneId);
                state->editor.selectedEntities.Clear();
                state->editor.selectedEntities.PushBack(copied);
                MarkSceneModified(state, state->currentSceneId);
            }
            ImGui::SameLine();
            const auto* prefabInst2 = state->registry.try_get<Component::PrefabInstanceComponent>(e.entity);
            const bool isPrefab = prefabInst2 != nullptr;
            const bool isMasterPrefab2 = isPrefab && prefabInst2->bMasterPrefab;
            bool selected = std::find(state->editor.selectedEntities.begin(), state->editor.selectedEntities.end(), e.entity) != state->editor.selectedEntities.end();

            if (isPrefab) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
            char uniqueLabel[256];
            if (isMasterPrefab2) {
                snprintf(uniqueLabel, sizeof(uniqueLabel), "[M] %s##%llu", e.label, e.stableId);
            }
            else {
                snprintf(uniqueLabel, sizeof(uniqueLabel), "%s##%llu", e.label, e.stableId);
            }
            if (ImGui::Selectable(uniqueLabel, selected)) {
                if (ImGui::GetIO().KeyCtrl) {
                    auto it = std::ranges::find(state->editor.selectedEntities, e.entity);
                    if (it != state->editor.selectedEntities.end()) {
                        state->editor.selectedEntities.Remove(it);
                    }
                    else {
                        state->editor.selectedEntities.PushBack(e.entity);
                    }
                }
                else {
                    state->editor.selectedEntities.Clear();
                    state->editor.selectedEntities.PushBack(e.entity);
                }
            }
            if (isPrefab) ImGui::PopStyleColor();
        };

        auto drawGroup = [&](Core::Span<EntityEntry*> group) {
            for (size_t i = 0; i < group.Size(); ++i) {
                const EntityEntry* prev = i > 0 ? group[i - 1] : nullptr;
                const EntityEntry* next = (i + 1 < group.Size()) ? group[i + 1] : nullptr;
                drawEntityRow(*group[i], prev, next);
            }
        };

        // Collect unique folder names at level 0, sorted alphabetically
        struct FolderInfo
        {
            StringID id;
            const char* name;
        };
        Core::ArenaVector<FolderInfo> folders0{&ctx->editorArena.Get(), 64};
        for (auto& e : entries) {
            if (!e.folder0.IsValid()) continue;
            bool found = false;
            for (auto& f : folders0) {
                if (f.id == e.folder0) {
                    found = true;
                    break;
                }
            }
            if (!found) folders0.PushBack({e.folder0, e.folderName0});
        }
        std::ranges::sort(folders0, [](const FolderInfo& a, const FolderInfo& b) { return strcmp(a.name, b.name) < 0; });

        // Draw unfoldered entities first
        {
            Core::ArenaVector<EntityEntry*> group{&ctx->editorArena.Get(), 1024};
            for (auto& e : entries) {
                if (!e.folder0.IsValid()) group.PushBack(&e);
            }
            drawGroup(group);
        }

        // Draw folder tree nodes
        for (auto& [id0, name0] : folders0) {
            if (ImGui::TreeNode(fmt::format("{}##folder_{}", name0, id0.id).c_str())) {
                // Collect subfolders for this folder, sorted alphabetically
                Core::ArenaVector<FolderInfo> subfolders{&ctx->editorArena.Get(), 64};
                for (auto& e : entries) {
                    if (e.folder0 != id0 || !e.folder1.IsValid()) continue;
                    bool found = false;
                    for (auto& sf : subfolders) {
                        if (sf.id == e.folder1) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) subfolders.PushBack({e.folder1, e.folderName1});
                }
                std::ranges::sort(subfolders, [](const FolderInfo& a, const FolderInfo& b) { return strcmp(a.name, b.name) < 0; });

                // Direct folder members (no subfolder)
                {
                    Core::ArenaVector<EntityEntry*> group{&ctx->editorArena.Get(), 1024};
                    for (auto& e : entries) {
                        if (e.folder0 == id0 && !e.folder1.IsValid()) group.PushBack(&e);
                    }
                    drawGroup(group);
                }

                // Subfolder tree nodes
                for (auto& [id1, name1] : subfolders) {
                    if (ImGui::TreeNode(fmt::format("{}##subfolder_{}_{}", name1, id0.id, id1.id).c_str())) {
                        Core::ArenaVector<EntityEntry*> group{&ctx->editorArena.Get(), 1024};
                        for (auto& e : entries) {
                            if (e.folder0 == id0 && e.folder1 == id1) group.PushBack(&e);
                        }
                        drawGroup(group);
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }

        if (entityToDelete != entt::null) {
            auto it = std::ranges::find(state->editor.selectedEntities, entityToDelete);
            if (it != state->editor.selectedEntities.end()) {
                state->editor.selectedEntities.Remove(it);
            }
            state->registry.destroy(entityToDelete);
            MarkSceneModified(state, state->currentSceneId);
        }
    }
    ImGui::End();

    glm::vec3 multiGizmoCentroid{0.0f};
    int transformCount = 0;
    for (auto entity : state->editor.selectedEntities) {
        if (auto* tf = state->registry.try_get<Component::TransformComponent>(entity)) {
            multiGizmoCentroid += tf->translation;
            ++transformCount;
        }
    }
    if (transformCount > 0)
        multiGizmoCentroid /= static_cast<float>(transformCount);

    if (ImGui::Begin("Details")) {
        if (state->editor.selectedEntities.Size() == 1) {
            Engine::ComponentEntry* entryToRemove = nullptr;
            entt::entity entity = state->editor.selectedEntities[0];
            ImGui::Text("Entity: %u", static_cast<uint32_t>(entity));
            if (const auto* stable = state->registry.try_get<Component::StableIdComponent>(entity)) {
                ImGui::SameLine();
                ImGui::TextDisabled("(order: %llu)", stable->sortOrder);
            }

            auto* entityScene = state->registry.try_get<Component::SceneComponent>(entity);
            for (Engine::ComponentEntry& entry : state->componentRegistry.registry) {
                if (entry.has(state->registry, entity)) {
                    nlohmann::json before;
                    entry.serialize(state->registry, entity, before);

                    Engine::ComponentEditorResult result = entry.drawEditor(frameBuffer->mainViewFamily, state->registry, entity, entry.name);

                    if (result.requestRemoval) {
                        entryToRemove = &entry;
                        if (entityScene) MarkSceneModified(state, entityScene->sceneId);
                    }
                    else {
                        nlohmann::json after;
                        entry.serialize(state->registry, entity, after);
                        if (before != after && entityScene) MarkSceneModified(state, entityScene->sceneId);
                    }
                }
            }
            if (entryToRemove) {
                entryToRemove->remove(state->registry, entity);
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
            DrawMultiSelectEditor(ctx, state, multiGizmoCentroid, transformCount);
        }
    }
    ImGui::End();

    if (!state->editor.bExclusiveGizmoActive && !bJustSelected && !state->editor.selectedEntities.IsEmpty()) {
        if (state->editor.selectedEntities.Size() == 1) {
            entt::entity entity = state->editor.selectedEntities[0];
            if (auto* transform = state->registry.try_get<Component::TransformComponent>(entity)) {
                auto model = Component::GetMatrix(*transform);
                float snapArr[3] = {};
                float* snap = nullptr;
                if (state->editor.bSnapEnabled) {
                    if (state->editor.currentGizmoOperation == ImGuizmo::TRANSLATE)
                        snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapTranslation;
                    else if (state->editor.currentGizmoOperation == ImGuizmo::ROTATE)
                        snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapRotation;
                    else
                        snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapScale;
                    snap = snapArr;
                }

                ImGuizmo::Manipulate(
                    glm::value_ptr(view),
                    glm::value_ptr(proj),
                    state->editor.currentGizmoOperation,
                    state->editor.currentGizmoMode,
                    glm::value_ptr(model),
                    nullptr,
                    snap
                );
                if (ImGuizmo::IsUsing()) {
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), t, r, s);
                    glm::vec3 translation = glm::vec3(t[0], t[1], t[2]);
                    if (state->editor.bSnapEnabled && state->editor.bSnapWorldGrid && state->editor.currentGizmoOperation == ImGuizmo::TRANSLATE) {
                        const float g = state->editor.snapTranslation;
                        translation = glm::round(translation / g) * g;
                    }
                    transform->translation = translation;
                    transform->rotation = glm::quat(glm::radians(glm::vec3(r[0], r[1], r[2])));
                    if (state->editor.bUniformScaleMode)
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
        else if (state->editor.currentGizmoOperation != ImGuizmo::SCALE) {
            static glm::vec3 s_prevTranslation{};
            static bool s_wasDragging = false;

            float snapArr[3] = {};
            float* snap = nullptr;
            if (state->editor.bSnapEnabled) {
                if (state->editor.currentGizmoOperation == ImGuizmo::TRANSLATE)
                    snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapTranslation;
                else
                    snapArr[0] = snapArr[1] = snapArr[2] = state->editor.snapRotation;
                snap = snapArr;
            }

            glm::mat4 gizmoMatrix = glm::translate(glm::mat4(1.0f), multiGizmoCentroid);
            glm::mat4 deltaMatrix(1.0f);
            ImGuizmo::Manipulate(
                glm::value_ptr(view),
                glm::value_ptr(proj),
                state->editor.currentGizmoOperation,
                ImGuizmo::WORLD,
                glm::value_ptr(gizmoMatrix),
                glm::value_ptr(deltaMatrix),
                snap
            );

            if (ImGuizmo::IsUsing()) {
                if (!s_wasDragging) {
                    s_prevTranslation = multiGizmoCentroid;
                    s_wasDragging = true;
                    for (auto e : state->editor.selectedEntities) {
                        if (auto* sc = state->registry.try_get<Component::SceneComponent>(e)) {
                            MarkSceneModified(state, sc->sceneId);
                        }
                    }
                }

                glm::vec3 deltaTranslation{0.0f};
                if (state->editor.currentGizmoOperation == ImGuizmo::TRANSLATE) {
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(gizmoMatrix), t, r, s);
                    glm::vec3 newT = {t[0], t[1], t[2]};
                    if (state->editor.bSnapEnabled && state->editor.bSnapWorldGrid) {
                        const float g = state->editor.snapTranslation;
                        newT = glm::round(newT / g) * g;
                    }
                    deltaTranslation = newT - s_prevTranslation;
                    s_prevTranslation = newT;
                }

                const glm::quat deltaRotation = glm::normalize(glm::quat_cast(glm::mat3(deltaMatrix)));

                for (auto entity : state->editor.selectedEntities) {
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
        bool bPPConfigChanged = false;
        constexpr Core::PostProcessConfiguration defaultPP{};

        bool& bAutoSavePP = state->projectConfig.bAutoSave;
        ImGui::BeginDisabled(bAutoSavePP);
        if (ImGui::Button("Save Config")) {
            state->projectConfig.aaMode = state->lighting.aaMode;
            state->projectConfig.gtaoConfig = state->lighting.gtaoConfig;
            state->projectConfig.smaaConfig = state->lighting.smaaConfig;
            state->projectConfig.postProcess = state->lighting.postProcess;
            state->projectConfig.restir = state->debug.restir;
            state->projectConfig.relax = state->debug.relax;
            Engine::WriteProjectConfig(state->projectConfig);
        }
        ImGui::EndDisabled();
        if (bAutoSavePP && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Auto-save is enabled");
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto-save##pp", &bAutoSavePP)) {
            Engine::WriteProjectConfig(state->projectConfig);
        }

        ImGui::Separator();
        if (ImGui::Button("Reset All to Defaults")) {
            state->lighting.postProcess = defaultPP;
            bPPConfigChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Disable All Effects")) {
            state->lighting.aaMode = Core::AntiAliasingMode::None;
            state->lighting.postProcess.tonemapOperator = -1;
            state->lighting.postProcess.bExposureEnabled = false;
            state->lighting.postProcess.bBloomEnabled = false;
            state->lighting.postProcess.bColorGradingEnabled = false;
            state->lighting.postProcess.bVignetteAberrationEnabled = false;
            state->lighting.postProcess.bSharpeningEnabled = false;
            state->lighting.postProcess.bPaniniEnabled = false;
            state->lighting.postProcess.bFilmGrainEnabled = false;
            state->lighting.postProcess.bDitherEnabled = false;
            bPPConfigChanged = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Ground Truth Ambient Occlusion");
        if (ImGui::Checkbox("Enable GTAO", &state->lighting.gtaoConfig.bEnabled)) { bPPConfigChanged = true; }

        ImGui::Spacing();
        ImGui::SeparatorText("Anti-Aliasing"); {
            const char* aaModes[] = {"None", "SMAA", "TAA", "SMAA T2X", "Naive TAA"};
            int currentAA = static_cast<int>(state->lighting.aaMode);
            if (ImGui::Combo("Mode##aa", &currentAA, aaModes, IM_ARRAYSIZE(aaModes))) {
                state->lighting.aaMode = static_cast<Core::AntiAliasingMode>(currentAA);
                bPPConfigChanged = true;
            }
            const bool bSMAA = state->lighting.aaMode == Core::AntiAliasingMode::SMAA || state->lighting.aaMode == Core::AntiAliasingMode::SMAAT2X;
            if (bSMAA) {
                Core::SMAAConfiguration& smaa = state->lighting.smaaConfig;
                constexpr Core::SMAAConfiguration defaultSMAA{};
                const char* edgeModes[] = {"Luma", "Color", "Depth"};
                int currentMode = static_cast<int>(smaa.edgeDetectionMode);
                if (ImGui::Combo("Edge Detection##smaa", &currentMode, edgeModes, IM_ARRAYSIZE(edgeModes))) {
                    smaa.edgeDetectionMode = static_cast<Core::SMAAEdgeDetectionMode>(currentMode);
                    bPPConfigChanged = true;
                }
                if (ImGui::SliderFloat("Threshold##smaa", &smaa.threshold, 0.01f, 0.5f, "%.3f")) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Local Contrast Adapt.##smaa", &smaa.localContrastAdaptation, 0.5f, 4.0f, "%.2f")) { bPPConfigChanged = true; }
                if (ImGui::SliderInt("Max Search Steps##smaa", &smaa.maxSearchSteps, 1, 112)) { bPPConfigChanged = true; }
                if (ImGui::SliderInt("Max Search Steps Diag##smaa", &smaa.maxSearchStepsDiag, 1, 20)) { bPPConfigChanged = true; }
                if (ImGui::Button("Reset SMAA")) {
                    smaa = defaultSMAA;
                    bPPConfigChanged = true;
                }
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Denoiser"); {
            Core::ReSTIRParams& restir = state->debug.restir;
            const char* denoiserModes[] = {"None", "A-Trous Wavelet", "A-SVGF", "RELAX"};
            int currentDenoiser = static_cast<int>(restir.denoiserMode);
            if (ImGui::Combo("Mode##denoiser", &currentDenoiser, denoiserModes, IM_ARRAYSIZE(denoiserModes))) {
                restir.denoiserMode = static_cast<Core::ReSTIRParams::DenoiserMode>(currentDenoiser);
                bPPConfigChanged = true;
            }

            const char* remodulateOutputModes[] = {"Both", "Diffuse Only", "Specular Only"};
            int currentRemodulateOutput = static_cast<int>(restir.remodulateOutput);
            if (ImGui::Combo("Remodulate Output##restir", &currentRemodulateOutput, remodulateOutputModes, IM_ARRAYSIZE(remodulateOutputModes))) {
                restir.remodulateOutput = static_cast<Core::ReSTIRParams::RemodulateOutput>(currentRemodulateOutput);
                bPPConfigChanged = true;
            }

            const bool bATrous = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ATrous;
            const bool bSVGF = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::ASVGF;
            const bool bRELAX = restir.denoiserMode == Core::ReSTIRParams::DenoiserMode::RELAX;

            if (bATrous) {
                ImGui::SeparatorText("A-Trous");
                if (ImGui::SliderInt("Iterations##atrous", &restir.atrous.iterations, 1, 4)) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Sigma Luminance##atrous", &restir.atrous.sigmaLuminance, 0.0f, 10.0f)) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Sigma Normal##atrous", &restir.atrous.sigmaNormal, 1.0f, 256.0f)) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Sigma Depth##atrous", &restir.atrous.sigmaDepth, 0.0001f, 1.0f)) { bPPConfigChanged = true; }
                if (ImGui::Button("Reset A-Trous")) {
                    restir.atrous = Core::ReSTIRParams::ATrousParams{};
                    bPPConfigChanged = true;
                }
            }
            if (bSVGF) {
                ImGui::SeparatorText("A-SVGF");
                if (ImGui::SliderInt("ATrous Iterations##svgf", &restir.svgf.atrousIterations, 0, 4)) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Alpha Min##svgf", &restir.svgf.alphaMin, 0.005f, 1.0f)) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Gradient Threshold##svgf", &restir.svgf.gradientThreshold, 0.0f, 0.2f)) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Sigma Luminance##svgf", &restir.svgf.sigmaLuminance, 0.1f, 20.0f)) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Sigma Normal##svgf", &restir.svgf.sigmaNormal, 1.0f, 256.0f)) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Sigma Depth##svgf", &restir.svgf.sigmaDepth, 0.0001f, 1.0f)) { bPPConfigChanged = true; }
                if (ImGui::Button("Reset A-SVGF")) {
                    restir.svgf = Core::ReSTIRParams::SVGFParams{};
                    bPPConfigChanged = true;
                }
            }
            if (bRELAX) {
                Core::RELAXParams& relax = state->debug.relax;
                ImGui::SeparatorText("RELAX");

                const float relaxInputW = 70.0f;
                const float relaxSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
                const float relaxResetW = ImGui::CalcTextSize("R").x + ImGui::GetStyle().FramePadding.x * 2.0f;
                static const Core::RELAXParams relaxDefaults{};
                auto relaxTip = [&](const char* tip) {
                    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("%s", tip);
                    }
                };
                auto relaxF = [&](const char* label, float* v, float def, float min, float max, const char* fmt = "%.4f", const char* tip = nullptr) {
                    ImGui::PushID(label);
                    float sliderW = ImGui::CalcItemWidth() - relaxInputW - relaxResetW - relaxSpacing * 2.0f;
                    if (sliderW < 60.0f) { sliderW = 60.0f; }
                    ImGui::SetNextItemWidth(sliderW);
                    if (ImGui::SliderFloat("##s", v, min, max, "")) { bPPConfigChanged = true; }
                    relaxTip(tip);
                    ImGui::SameLine(0.0f, relaxSpacing);
                    ImGui::SetNextItemWidth(relaxInputW);
                    if (ImGui::InputFloat("##i", v, 0.0f, 0.0f, fmt)) { bPPConfigChanged = true; }
                    ImGui::SameLine(0.0f, relaxSpacing);
                    if (ImGui::Button("R")) { *v = def; bPPConfigChanged = true; }
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Reset to %g", def); }
                    ImGui::SameLine(0.0f, relaxSpacing);
                    ImGui::TextUnformatted(label);
                    relaxTip(tip);
                    ImGui::PopID();
                };
                auto relaxI = [&](const char* label, int* v, int def, int min, int max, const char* tip = nullptr) {
                    ImGui::PushID(label);
                    float sliderW = ImGui::CalcItemWidth() - relaxInputW - relaxResetW - relaxSpacing * 2.0f;
                    if (sliderW < 60.0f) { sliderW = 60.0f; }
                    ImGui::SetNextItemWidth(sliderW);
                    if (ImGui::SliderInt("##s", v, min, max, "")) { bPPConfigChanged = true; }
                    relaxTip(tip);
                    ImGui::SameLine(0.0f, relaxSpacing);
                    ImGui::SetNextItemWidth(relaxInputW);
                    if (ImGui::InputInt("##i", v, 0, 0)) { bPPConfigChanged = true; }
                    ImGui::SameLine(0.0f, relaxSpacing);
                    if (ImGui::Button("R")) { *v = def; bPPConfigChanged = true; }
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Reset to %d", def); }
                    ImGui::SameLine(0.0f, relaxSpacing);
                    ImGui::TextUnformatted(label);
                    relaxTip(tip);
                    ImGui::PopID();
                };

                if (ImGui::Checkbox("Prepass##relax", &relax.enablePrepass)) { bPPConfigChanged = true; }
                relaxTip("Spatial pre-blur before temporal accumulation, lowering the input noise fed into history. Default on.");
                ImGui::SameLine();
                if (ImGui::Checkbox("Anti-Firefly##relax", &relax.enableAntiFirefly)) { bPPConfigChanged = true; }
                relaxTip("Suppresses isolated bright outlier pixels (fireflies) before accumulation. Default on.");
                if (ImGui::Checkbox("Roughness Edge Stopping##relax", &relax.roughnessEdgeStoppingEnabled)) { bPPConfigChanged = true; }
                relaxTip("Roughness-aware specular edge stopping (roughness + oriented-normal weights). Off uses a simpler normal-only weight. Default on.");

                ImGui::SeparatorText("General");
                relaxF("Denoising Range", &relax.denoisingRange, relaxDefaults.denoisingRange, 10.f, 5000.f, "%.1f", "Max view-space distance (world units) that gets denoised; farther surfaces pass through untouched. Default 1000; set to roughly cover your scene depth.");
                relaxF("Disocclusion Threshold", &relax.disocclusionThreshold, relaxDefaults.disocclusionThreshold, 0.001f, 0.05f, "%.4f", "Relative depth tolerance for accepting reprojected history. Higher accepts more (less ghosting rejection); lower resets more on edges/motion. Default 0.005; common ~0.01.");
                relaxF("Depth Threshold", &relax.depthThreshold, relaxDefaults.depthThreshold, 0.0f, 0.05f, "%.4f", "Plane-distance tolerance for spatial edge stopping, as a fraction of depth. Lower preserves geometry edges; higher blurs across them. Default 0.003.");
                relaxF("Framerate Scale", &relax.framerateScale, relaxDefaults.framerateScale, 0.1f, 4.f, "%.2f", "Scales accumulation/anti-lag speed for framerate (roughly currentFPS/60). 1.0 is tuned for 60 FPS; raise at higher FPS so history doesn't over-accumulate. Default 1.0.");

                ImGui::SeparatorText("Accumulation");
                relaxF("Spec Max Accum Frames", &relax.specMaxAccumFrames, relaxDefaults.specMaxAccumFrames, 0.f, 64.f, "%.0f", "Max specular history length (stable). Higher = cleaner but laggier reflections. Default 32; common 30-60.");
                relaxF("Spec Max Fast Accum Frames", &relax.specMaxFastAccumFrames, relaxDefaults.specMaxFastAccumFrames, 0.f, 16.f, "%.0f", "Length of the noisy 'fast' specular history used to clamp the slow one (anti-lag). Must be below Spec Max Accum to enable clamping. Default 4; common 4-6.");
                relaxF("Diff Max Accum Frames", &relax.diffMaxAccumFrames, relaxDefaults.diffMaxAccumFrames, 0.f, 64.f, "%.0f", "Max diffuse history length (stable). Higher = cleaner but slower to react to lighting changes (more lag). Default 32; common 30-60.");
                relaxF("Diff Max Fast Accum Frames", &relax.diffMaxFastAccumFrames, relaxDefaults.diffMaxFastAccumFrames, 0.f, 16.f, "%.0f", "Length of the noisy 'fast' diffuse history used to clamp the slow one (anti-lag). Lower = snappier response. Must be below Diff Max Accum. Default 4; common 4-6.");
                relaxF("History Acceleration Amount", &relax.historyAccelerationAmount, relaxDefaults.historyAccelerationAmount, 0.f, 1.f, "%.2f", "Strength of anti-lag acceleration pushing the slow history toward the fast one on changes. 0 = off, 1 = max. Default 1.0.");

                ImGui::SeparatorText("Prepass");
                relaxF("Diff Blur Radius", &relax.diffBlurRadius, relaxDefaults.diffBlurRadius, 0.f, 100.f, "%.1f", "Radius (px) of the diffuse pre-blur applied before accumulation. Larger knocks down more input noise but loses detail. 0 disables. Default 30.");
                relaxF("Spec Blur Radius", &relax.specBlurRadius, relaxDefaults.specBlurRadius, 0.f, 100.f, "%.1f", "Radius (px) of the specular pre-blur before accumulation. 0 disables. Default 50.");
                relaxF("Min Hit Distance Weight", &relax.minHitDistanceWeight, relaxDefaults.minHitDistanceWeight, 0.f, 1.f, "%.2f", "Minimum weight for ray hit-distance when reconstructing specular in the prepass. 0 ignores hitT. Default 0; NRD commonly ~0.1-0.2.");

                ImGui::SeparatorText("A-Trous / Edge Stopping");
                relaxI("ATrous Iterations", &relax.atrousIterations, relaxDefaults.atrousIterations, 1, 5, "Number of A-trous wavelet (spatial) passes. More = wider, smoother denoising but blurrier and costlier. Default 3; common 4-5.");
                relaxF("Lobe Angle Fraction", &relax.lobeAngleFraction, relaxDefaults.lobeAngleFraction, 0.f, 1.f, "%.3f", "Normal edge-stopping tolerance, as a fraction of the BRDF lobe angle. Lower preserves sharper normal detail; higher blurs across normals. Default 0.15.");
                relaxF("Roughness Fraction", &relax.roughnessFraction, relaxDefaults.roughnessFraction, 0.f, 1.f, "%.3f", "Roughness edge-stopping tolerance (fraction). Higher blends across differing roughness; lower keeps roughness boundaries crisp. Default 0.15.");
                relaxF("Spec Lobe Angle Slack", &relax.specLobeAngleSlack, relaxDefaults.specLobeAngleSlack, 0.f, 1.f, "%.3f", "Extra angular slack added to the specular lobe for edge stopping, loosening normal/view rejection. Default 0.15.");
                relaxF("Spec Phi Luminance", &relax.specPhiLuminance, relaxDefaults.specPhiLuminance, 0.f, 10.f, "%.2f", "Specular luminance edge-stopping sensitivity (sigma scale). Higher = more blur (ignores luminance diffs); lower preserves highlights. Default 2.0; common 1-2.");
                relaxF("Diff Phi Luminance", &relax.diffPhiLuminance, relaxDefaults.diffPhiLuminance, 0.f, 10.f, "%.2f", "Diffuse luminance edge-stopping sensitivity (sigma scale). Higher = more blur; lower keeps luminance edges. Default 2.0; common 1-2.");
                relaxF("Diff Max Lum Rel Diff", &relax.diffMaxLuminanceRelativeDifference, relaxDefaults.diffMaxLuminanceRelativeDifference, 0.f, 10.f, "%.2f", "Caps how strongly a luminance difference can reject a diffuse sample (in sigmas). Lower = firmer edge stopping. Default 3.");
                relaxF("Spec Max Lum Rel Diff", &relax.specMaxLuminanceRelativeDifference, relaxDefaults.specMaxLuminanceRelativeDifference, 0.f, 10.f, "%.2f", "Caps how strongly a luminance difference can reject a specular sample (in sigmas). Default 3.");
                relaxF("Luminance Edge Stop Relax", &relax.luminanceEdgeStoppingRelaxation, relaxDefaults.luminanceEdgeStoppingRelaxation, 0.f, 1.f, "%.2f", "On early A-trous passes, relaxes specular luminance edge stopping where reprojection confidence is low (helps fresh/disoccluded pixels). 0-1. Default 0.5.");
                relaxF("Normal Edge Stop Relax", &relax.normalEdgeStoppingRelaxation, relaxDefaults.normalEdgeStoppingRelaxation, 0.f, 1.f, "%.2f", "Relaxes specular normal edge stopping based on reprojection confidence, cutting noise on low-confidence pixels. 0-1. Default 0.3.");
                relaxF("Roughness Edge Stop Relax", &relax.roughnessEdgeStoppingRelaxation, relaxDefaults.roughnessEdgeStoppingRelaxation, 0.f, 1.f, "%.2f", "Relaxes the view vector used in specular weighting, loosening rejection on curved/rough surfaces. Default 0.3.");
                relaxF("Spec Variance Boost", &relax.specVarianceBoost, relaxDefaults.specVarianceBoost, 0.f, 8.f, "%.2f", "Boosts specular variance while history is short so fresh pixels filter more aggressively. 1 = no boost. Default 1.0.");

                ImGui::SeparatorText("History Fix");
                relaxF("Hist Fix Edge Stop Normal Pow", &relax.historyFixEdgeStoppingNormalPower, relaxDefaults.historyFixEdgeStoppingNormalPower, 0.f, 32.f, "%.1f", "Normal-match strictness for the history-fix fill that bootstraps fresh pixels. Higher = stricter normal matching. Default 8.");
                relaxF("Hist Fix Frame Num", &relax.historyFixFrameNum, relaxDefaults.historyFixFrameNum, 0.f, 32.f, "%.1f", "Pixels with history shorter than this get a sparse spatial fill (bootstrap) instead of relying on accumulation. 0 disables. Default 4.");
                relaxF("Hist Fix Base Pixel Stride", &relax.historyFixBasePixelStride, relaxDefaults.historyFixBasePixelStride, 0.f, 32.f, "%.1f", "Base sample spacing (px) for the history-fix fill; shrinks as history grows. Larger = wider initial fill. Default 14.");

                ImGui::SeparatorText("History Clamp / Reset");
                relaxF("Fast History Clamp Sigma", &relax.fastHistoryClampingSigmaScale, relaxDefaults.fastHistoryClampingSigmaScale, 0.f, 8.f, "%.2f", "Width (in sigmas) of the fast-history color box that clamps the slow history (anti-lag/anti-ghosting). Lower = tighter clamp, less lag but more noise. Default 2.0; common 1-2.");
                relaxF("History Reset Temporal Sigma", &relax.historyResetTemporalSigmaScale, relaxDefaults.historyResetTemporalSigmaScale, 0.f, 10.f, "%.2f", "Temporal noise sigma scale in history-reset detection; larger tolerates more temporal noise before resetting. Default 5.");
                relaxF("History Reset Spatial Sigma", &relax.historyResetSpatialSigmaScale, relaxDefaults.historyResetSpatialSigmaScale, 0.f, 10.f, "%.2f", "Spatial noise sigma scale in history-reset detection; larger tolerates more spatial noise before resetting. Default 1.");
                relaxF("History Reset Amount", &relax.historyResetAmount, relaxDefaults.historyResetAmount, 0.f, 1.f, "%.2f", "How hard to snap history to the current noisy signal on big lighting changes. 0 = off (rely on clamping); 1 = aggressive. Default 0.5.");

                ImGui::Spacing();
                if (ImGui::Button("Reset RELAX")) {
                    relax = Core::RELAXParams{};
                    bPPConfigChanged = true;
                }
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Tonemapping");
        const char* tonemapOperators[] = {"None", "[Simple] ACES (Hill)", "[Simple] Hable", "[Simple] Reinhard", "[Simple] Lottes", "[Simple] Reinhard-Jodie", "[Simple] Clamp", "[Filmic] Hejl-Burgess-Dawson", "[Filmic] Uchimura", "[Filmic] ACES (Narkowicz)", "[Modern] AgX", "[Modern] Khronos PBR Neutral"};
        int currentItem = state->lighting.postProcess.tonemapOperator + 1;
        if (ImGui::Combo("Operator", &currentItem, tonemapOperators, IM_ARRAYSIZE(tonemapOperators))) {
            state->lighting.postProcess.tonemapOperator = currentItem - 1;
            bPPConfigChanged = true;
        }

        Core::PostProcessConfiguration& pp = state->lighting.postProcess;
        switch (pp.tonemapOperator) {
            case 1: // Hable
                if (ImGui::SliderFloat("White Point##hable", &pp.hableParams.whitePoint, 1.0f, 20.0f, "%.2f")) { bPPConfigChanged = true; }
                break;
            case 2: // Reinhard
                if (ImGui::SliderFloat("White Point##reinhard", &pp.reinhardParams.whitePoint, 1.0f, 20.0f, "%.2f")) { bPPConfigChanged = true; }
                break;
            case 7: // Uchimura
                if (ImGui::SliderFloat("Max Brightness##uchimura", &pp.uchimuraParams.P, 0.5f, 2.0f, "%.2f")) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Contrast##uchimura", &pp.uchimuraParams.a, 0.5f, 2.0f, "%.2f")) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Linear Start##uchimura", &pp.uchimuraParams.m, 0.0f, 0.5f, "%.3f")) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Linear Length##uchimura", &pp.uchimuraParams.l, 0.0f, 1.0f, "%.2f")) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Toe Power##uchimura", &pp.uchimuraParams.c, 0.5f, 3.0f, "%.2f")) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Pedestal##uchimura", &pp.uchimuraParams.b, 0.0f, 0.1f, "%.3f")) { bPPConfigChanged = true; }
                break;
            case 9: // AgX
                if (ImGui::SliderFloat("Min EV##agx", &pp.agxParams.minEV, -20.0f, -1.0f, "%.3f")) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Max EV##agx", &pp.agxParams.maxEV, 0.0f, 10.0f, "%.3f")) { bPPConfigChanged = true; }
                break;
            case 10: // Khronos PBR Neutral
                if (ImGui::SliderFloat("Start Compression##khronos", &pp.khronosParams.startCompression, 0.5f, 0.95f, "%.3f")) { bPPConfigChanged = true; }
                if (ImGui::SliderFloat("Desaturation##khronos", &pp.khronosParams.desaturation, 0.0f, 0.5f, "%.3f")) { bPPConfigChanged = true; }
                break;
            default:
                break;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Exposure");
        if (ImGui::Checkbox("Enabled##exposure", &state->lighting.postProcess.bExposureEnabled)) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Target Luminance", &state->lighting.postProcess.exposureTargetLuminance, 0.01f, 1.0f, "%.3f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Adaptation Speed", &state->lighting.postProcess.exposureAdaptationRate, 0.1f, 50.0f, "%.1f")) { bPPConfigChanged = true; }
        if (ImGui::Button("Reset Exposure")) {
            state->lighting.postProcess.exposureTargetLuminance = defaultPP.exposureTargetLuminance;
            state->lighting.postProcess.exposureAdaptationRate = defaultPP.exposureAdaptationRate;
            bPPConfigChanged = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Bloom");
        if (ImGui::Checkbox("Enabled##bloom", &state->lighting.postProcess.bBloomEnabled)) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Intensity", &state->lighting.postProcess.bloomIntensity, 0.0f, 0.2f, "%.3f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Threshold", &state->lighting.postProcess.bloomThreshold, 0.0f, 2.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Soft Threshold", &state->lighting.postProcess.bloomSoftThreshold, 0.0f, 1.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Radius", &state->lighting.postProcess.bloomRadius, 0.5f, 2.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Clamp", &state->lighting.postProcess.bloomClamp, 0.1f, 100.0f, "%.1f")) { bPPConfigChanged = true; }
        if (ImGui::Button("Reset Bloom")) {
            state->lighting.postProcess.bloomIntensity = defaultPP.bloomIntensity;
            state->lighting.postProcess.bloomThreshold = defaultPP.bloomThreshold;
            state->lighting.postProcess.bloomSoftThreshold = defaultPP.bloomSoftThreshold;
            state->lighting.postProcess.bloomRadius = defaultPP.bloomRadius;
            state->lighting.postProcess.bloomClamp = defaultPP.bloomClamp;
            bPPConfigChanged = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Motion Blur");
        if (ImGui::DragFloat("Velocity Scale", &state->lighting.postProcess.motionBlurVelocityScale, 0.05f, 0.0f, 4.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::DragFloat("Depth Scale", &state->lighting.postProcess.motionBlurDepthScale, 0.1f, 2.0f, 10.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::Button("Reset Motion Blur")) {
            state->lighting.postProcess.motionBlurVelocityScale = defaultPP.motionBlurVelocityScale;
            state->lighting.postProcess.motionBlurDepthScale = defaultPP.motionBlurDepthScale;
            bPPConfigChanged = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Color Grading");
        if (ImGui::Checkbox("Enabled##colorgrading", &state->lighting.postProcess.bColorGradingEnabled)) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Exposure Offset", &state->lighting.postProcess.colorGradingExposure, -2.0f, 2.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Contrast", &state->lighting.postProcess.colorGradingContrast, 0.5f, 2.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Saturation", &state->lighting.postProcess.colorGradingSaturation, 0.0f, 2.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Temperature", &state->lighting.postProcess.colorGradingTemperature, -1.0f, 1.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Tint", &state->lighting.postProcess.colorGradingTint, -1.0f, 1.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::Button("Reset Color Grading")) {
            state->lighting.postProcess.colorGradingExposure = defaultPP.colorGradingExposure;
            state->lighting.postProcess.colorGradingContrast = defaultPP.colorGradingContrast;
            state->lighting.postProcess.colorGradingSaturation = defaultPP.colorGradingSaturation;
            state->lighting.postProcess.colorGradingTemperature = defaultPP.colorGradingTemperature;
            state->lighting.postProcess.colorGradingTint = defaultPP.colorGradingTint;
            bPPConfigChanged = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Vignette & Chromatic Aberration");
        if (ImGui::Checkbox("Enabled##vigab", &state->lighting.postProcess.bVignetteAberrationEnabled)) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Aberration Strength", &state->lighting.postProcess.chromaticAberrationStrength, 0.0f, 100.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Vignette Strength", &state->lighting.postProcess.vignetteStrength, 0.0f, 1.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Vignette Radius", &state->lighting.postProcess.vignetteRadius, 0.5f, 1.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Vignette Smoothness", &state->lighting.postProcess.vignetteSmoothness, 0.1f, 1.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::Button("Reset Vignette & Aberration")) {
            state->lighting.postProcess.chromaticAberrationStrength = defaultPP.chromaticAberrationStrength;
            state->lighting.postProcess.vignetteStrength = defaultPP.vignetteStrength;
            state->lighting.postProcess.vignetteRadius = defaultPP.vignetteRadius;
            state->lighting.postProcess.vignetteSmoothness = defaultPP.vignetteSmoothness;
            bPPConfigChanged = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Sharpening");
        if (ImGui::Checkbox("Enabled##sharpening", &state->lighting.postProcess.bSharpeningEnabled)) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Sharpening Strength", &state->lighting.postProcess.sharpeningStrength, 0.0f, 100.0f, "%.02f")) { bPPConfigChanged = true; }
        if (ImGui::Button("Reset Sharpening")) {
            state->lighting.postProcess.sharpeningStrength = defaultPP.sharpeningStrength;
            bPPConfigChanged = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Panini Projection");
        if (ImGui::Checkbox("Enabled##panini", &state->lighting.postProcess.bPaniniEnabled)) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Panini Strength", &state->lighting.postProcess.paniniStrength, 0.0f, 1.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::Button("Reset Panini")) {
            state->lighting.postProcess.paniniStrength = defaultPP.paniniStrength;
            bPPConfigChanged = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Film Grain");
        if (ImGui::Checkbox("Enabled##filmgrain", &state->lighting.postProcess.bFilmGrainEnabled)) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Grain Strength", &state->lighting.postProcess.grainStrength, 0.0f, 0.15f, "%.3f")) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Grain Size", &state->lighting.postProcess.grainSize, 1.0f, 3.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::Button("Reset Grain")) {
            state->lighting.postProcess.grainStrength = defaultPP.grainStrength;
            state->lighting.postProcess.grainSize = defaultPP.grainSize;
            bPPConfigChanged = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Dither");
        if (ImGui::Checkbox("Enabled##dither", &state->lighting.postProcess.bDitherEnabled)) { bPPConfigChanged = true; }
        if (ImGui::SliderFloat("Dither Strength", &state->lighting.postProcess.ditherStrength, 0.0f, 4.0f, "%.2f")) { bPPConfigChanged = true; }
        if (ImGui::Button("Reset Dither")) {
            state->lighting.postProcess.ditherStrength = defaultPP.ditherStrength;
            bPPConfigChanged = true;
        }

        if (bPPConfigChanged && state->projectConfig.bAutoSave) {
            state->projectConfig.aaMode = state->lighting.aaMode;
            state->projectConfig.gtaoConfig = state->lighting.gtaoConfig;
            state->projectConfig.smaaConfig = state->lighting.smaaConfig;
            state->projectConfig.postProcess = state->lighting.postProcess;
            state->projectConfig.restir = state->debug.restir;
            state->projectConfig.relax = state->debug.relax;
            Engine::WriteProjectConfig(state->projectConfig);
        }
    }
    ImGui::End();

    if (ImGui::Begin("Scene")) {
        ImGui::Checkbox("Enable Physics", &state->physics.bEnabled);

        if (ImGui::CollapsingHeader("Shadow Settings")) {
            const char* qualityNames[] = {"Ultra", "High", "Medium", "Low", "Custom"};
            int currentQuality = static_cast<int>(state->lighting.shadowQuality);
            if (ImGui::Combo("Quality", &currentQuality, qualityNames, 5)) {
                state->lighting.shadowQuality = static_cast<Core::ShadowQuality>(currentQuality);
                if (currentQuality < 4) {
                    state->lighting.shadowConfig.cascadePreset = Render::SHADOW_PRESETS[currentQuality];
                }
            }

            ImGui::SliderFloat("Shadow Intensity", &state->lighting.shadowConfig.shadowIntensity, 0.0f, 1.0f);

            ImGui::Separator();
            ImGui::Text("Current Configuration:");
            for (int i = 0; i < 4; ++i) {
                ImGui::Text("Cascade %d:", i);
                ImGui::Indent();
                ImGui::Text("  Resolution: %dx%d",
                            state->lighting.shadowConfig.cascadePreset.extents[i][0],
                            state->lighting.shadowConfig.cascadePreset.extents[i][1]);
                ImGui::Text("  Bias: %.2f/%.2f",
                            state->lighting.shadowConfig.cascadePreset.biases[i].linear,
                            state->lighting.shadowConfig.cascadePreset.biases[i].sloped);
                ImGui::Text("  PCSS Samples: %u blocker, %u PCF",
                            state->lighting.shadowConfig.cascadePreset.pcssSamples[i].blockerSearchSamples,
                            state->lighting.shadowConfig.cascadePreset.pcssSamples[i].pcfSamples);
                ImGui::Text("  Light Size: %.4f",
                            state->lighting.shadowConfig.cascadePreset.lightSizes[i]);
                ImGui::Unindent();
            }

            if (state->lighting.shadowQuality == Core::ShadowQuality::Custom) {
                ImGui::Separator();
                ImGui::Text("Custom Settings:");

                static Render::ShadowCascadePreset customPreset = state->lighting.shadowConfig.cascadePreset;

                for (int i = 0; i < 4; ++i) {
                    ImGui::PushID(i);
                    if (ImGui::TreeNode("Cascade", "Cascade %d", i)) {
                        ImGui::InputInt("Width", reinterpret_cast<int*>(&customPreset.extents[i][0]));
                        ImGui::InputInt("Height", reinterpret_cast<int*>(&customPreset.extents[i][1]));
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
                    state->lighting.shadowConfig.cascadePreset = customPreset;
                }
            }

            ImGui::Separator();
            ImGui::SliderFloat("Split Lambda", &state->lighting.shadowConfig.splitLambda, 0.0f, 1.0f);
            ImGui::SliderFloat("Split Overlap", &state->lighting.shadowConfig.splitOverlap, 1.0f, 1.2f);
            ImGui::Checkbox("Enabled", &state->lighting.shadowConfig.enabled);
        }
    }
    ImGui::End();

    if (ImGui::Begin("Materials")) {
        Engine::MaterialManager* materialManager = ctx->materialManager;

        if (ImGui::BeginTabBar("##MaterialTabs")) {
            if (ImGui::BeginTabItem("Mesh Materials")) {
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
                    if (!mat.immutable) { ++mutableCount; }
                }
                ImGui::SeparatorText(fmt::format("Materials ({})", mutableCount).c_str());

                static Engine::MaterialID matRenameActive = Engine::MaterialID::INVALID;
                static char matRenameBuffer[128] = {};

                Engine::MaterialID materialPendingDelete = Engine::MaterialID::INVALID;
                for (const auto& [id, mat] : allMaterials) {
                    if (mat.immutable) continue;
                    ImGui::PushID(static_cast<int>(id.id));
                    if (ImGui::CollapsingHeader(mat.name.c_str())) {
                        ImGui::BeginDisabled(true);
                        ImGui::Text("ID: %llu", id.id);
                        ImGui::EndDisabled(); {
                            const auto& entryMap = materialManager->GetIdToEntryMap();
                            const bool materialInUse = entryMap.Contains(id) && materialManager->GetActiveMaterials()[entryMap.At(id)].refCounter > 0;
                            ImGui::BeginDisabled(materialInUse);
                            if (ImGui::Button("Delete Material")) {
                                materialPendingDelete = id;
                            }
                            ImGui::EndDisabled();
                            if (materialInUse && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                ImGui::SetTooltip("Material is referenced by scene entities");
                            }
                        }
                        ImGui::SameLine();
                        const bool isRenaming = matRenameActive == id;
                        if (isRenaming) {
                            ImGui::SetNextItemWidth(180.0f);
                            ImGui::InputText("##rename", matRenameBuffer, sizeof(matRenameBuffer));
                            ImGui::SameLine();
                            const bool nameUnchanged = strcmp(matRenameBuffer, mat.name.c_str()) == 0;
                            const bool nameEmpty = matRenameBuffer[0] == '\0';
                            const bool nameExists = !nameEmpty && !nameUnchanged && materialManager->FindMutableMaterial(StringID{matRenameBuffer, strlen(matRenameBuffer)}).IsValid();
                            ImGui::BeginDisabled(nameEmpty || nameUnchanged || nameExists);
                            if (ImGui::Button("Apply")) {
                                materialManager->RenameMutableMaterial(id, matRenameBuffer);
                                matRenameActive = Engine::MaterialID::INVALID;
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            if (ImGui::Button("Cancel")) {
                                matRenameActive = Engine::MaterialID::INVALID;
                            }
                            if (nameExists) {
                                ImGui::SameLine();
                                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
                            }
                        }
                        else {
                            if (ImGui::Button("Rename")) {
                                matRenameActive = id;
                                const auto& n = mat.name;
                                const size_t copyLen = std::min(n.Size(), sizeof(matRenameBuffer) - 1);
                                memcpy(matRenameBuffer, n.c_str(), copyLen);
                                matRenameBuffer[copyLen] = '\0';
                            }
                        }

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

                        ImGui::SeparatorText("Shader"); {
                            Core::Span<const StringID> shadingPipelines = ctx->pipelineManager->GetShadingPipelines();
                            const int32_t pipelineCount = static_cast<int32_t>(shadingPipelines.Size());
                            Core::Arena& arena = ctx->editorArena.Get();

                            int currentShader = -1;
                            for (int32_t i = 0; i < pipelineCount; ++i) {
                                if (editMat.fragmentShader == shadingPipelines[i]) {
                                    currentShader = i;
                                    break;
                                }
                            }

                            const bool isUnknown = currentShader < 0;
                            const int32_t optionCount = isUnknown ? pipelineCount + 1 : pipelineCount;
                            Core::ArenaArray<Core::InlineString<> > labels(&arena, optionCount);
                            for (int32_t i = 0; i < pipelineCount; ++i) { labels[i] = Core::InlineString(shadingPipelines[i].ToString()); }
                            if (isUnknown) {
                                labels[pipelineCount] = Core::InlineString("(unknown) ");
                                labels[pipelineCount].Append(editMat.fragmentShader.ToString());
                                currentShader = pipelineCount;
                            }
                            auto shadingGetter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<> >*>(data))[idx].c_str(); };
                            if (ImGui::Combo("Fragment Shader", &currentShader, shadingGetter, &labels, static_cast<int32_t>(labels.Size()))) {
                                if (currentShader < pipelineCount) {
                                    editMat.fragmentShader = shadingPipelines[currentShader];
                                    changed = true;
                                }
                            }
                        } {
                            Core::Span<const StringID> lightingPipelines = ctx->pipelineManager->GetLightingPipelines();
                            const int32_t pipelineCount = static_cast<int32_t>(lightingPipelines.Size());
                            Core::Arena& arena = ctx->editorArena.Get();

                            int currentShader = -1;
                            for (int32_t i = 0; i < pipelineCount; ++i) {
                                if (editMat.lightingShader == lightingPipelines[i]) {
                                    currentShader = i;
                                    break;
                                }
                            }

                            const bool isUnknown = currentShader < 0;
                            const int32_t optionCount = isUnknown ? pipelineCount + 1 : pipelineCount;
                            Core::ArenaArray<Core::InlineString<> > labels(&arena, optionCount);
                            for (int32_t i = 0; i < pipelineCount; ++i) { labels[i] = Core::InlineString(lightingPipelines[i].ToString()); }
                            if (isUnknown) {
                                labels[pipelineCount] = Core::InlineString("(unknown) ");
                                labels[pipelineCount].Append(editMat.lightingShader.ToString());
                                currentShader = pipelineCount;
                            }
                            auto lightingGetter = [](void* data, int idx) -> const char* { return (*static_cast<Core::ArenaArray<Core::InlineString<> >*>(data))[idx].c_str(); };
                            if (ImGui::Combo("Lighting Shader", &currentShader, lightingGetter, &labels, static_cast<int32_t>(labels.Size()))) {
                                if (currentShader < pipelineCount) {
                                    editMat.lightingShader = lightingPipelines[currentShader];
                                    changed = true;
                                }
                            }
                        }

                        ImGui::SeparatorText("Textures");
                        static const char* slotNames[] = {"Color", "Metal/Rough", "Normal", "Emissive", "Occlusion", "Packed NRM"};
                        static Engine::TextureID texEditPending = Engine::TextureID::INVALID;
                        static Engine::SamplerDesc samplerEditPending{};

                        if (!state->editor.textureInfoCache) {
                            const uint32_t count = ctx->assetManager->GetTextureInfoCount();
                            state->editor.textureInfoCache = ctx->editorArena.Get().Alloc<Core::ArenaFixedMap<Engine::TextureID, Engine::AssetManager::EditorTextureInfo> >(&ctx->editorArena.Get(), count);
                            ctx->assetManager->GetAllTextureInfos(*state->editor.textureInfoCache);
                        }
                        const auto& matTexInfoMap = *state->editor.textureInfoCache;

                        for (int32_t slot = 0; slot < 6; ++slot) {
                            ImGui::PushID(slot);

                            const Engine::TextureID& texId = mat.textureRefs[slot];
                            const char* currentTexName = "None";
                            if (const Engine::AssetManager::EditorTextureInfo* info = texId.IsValid() ? matTexInfoMap.Find(texId) : nullptr) {
                                currentTexName = info->name.c_str();
                            }

                            ImGui::Text("%-13s", slotNames[slot]);
                            ImGui::SameLine();
                            ImGui::TextDisabled("|");
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Tex")) {
                                texEditPending = texId;
                                ImGui::OpenPopup("TextureSelect");
                            }
                            ImGui::SameLine();
                            ImGui::Text("%-32s", currentTexName);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Smp")) {
                                samplerEditPending = mat.samplerDesc[slot];
                                ImGui::OpenPopup("SamplerEdit");
                            }
                            ImGui::SameLine(); {
                                const Engine::SamplerDesc& sd = mat.samplerDesc[slot];
                                const Engine::SamplerDesc def{};
                                const bool filterDiff = sd.magFilter != def.magFilter || sd.minFilter != def.minFilter;
                                const bool mipDiff = sd.mipmapMode != def.mipmapMode;
                                const bool addrDiff = sd.addressModeU != def.addressModeU || sd.addressModeV != def.addressModeV || sd.addressModeW != def.addressModeW;
                                const bool anisoDiff = sd.anisotropyEnable != def.anisotropyEnable || sd.maxAnisotropy != def.maxAnisotropy;
                                const bool lodDiff = sd.mipLodBias != def.mipLodBias || sd.minLod != def.minLod || sd.maxLod != def.maxLod;
                                const int diffCount = filterDiff + mipDiff + addrDiff + anisoDiff + lodDiff;
                                const char* label = "Default";
                                if (diffCount == 1) {
                                    if (filterDiff) { label = "Custom Filter"; }
                                    else if (mipDiff) { label = "Custom Mip Mode"; }
                                    else if (addrDiff) { label = "Custom Address"; }
                                    else if (anisoDiff) { label = "Custom Aniso"; }
                                    else if (lodDiff) { label = "Custom LOD"; }
                                }
                                else if (diffCount > 1) {
                                    label = "Custom Sampler";
                                }
                                ImGui::TextDisabled("%s", label);
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
                                    if (!state->editor.textureInfoCache) {
                                        const uint32_t count = ctx->assetManager->GetTextureInfoCount();
                                        state->editor.textureInfoCache = ctx->editorArena.Get().Alloc<Core::ArenaFixedMap<Engine::TextureID, Engine::AssetManager::EditorTextureInfo> >(&ctx->editorArena.Get(), count);
                                        ctx->assetManager->GetAllTextureInfos(*state->editor.textureInfoCache);
                                    }
                                    const uint32_t texCount = static_cast<uint32_t>(state->editor.textureInfoCache->Size());
                                    auto sorted = Core::ArenaFixedVector<Engine::AssetManager::EditorTextureInfo>(&ctx->editorArena.Get(), texCount);
                                    for (const auto& [id2, info] : *state->editor.textureInfoCache) {
                                        sorted.EmplaceBack(info);
                                    }
                                    std::ranges::sort(sorted, {}, &Engine::AssetManager::EditorTextureInfo::name);
                                    for (const auto& info : sorted) {
                                        const auto& id2 = info.id;
                                        const auto& name = info.name;
                                        bool selected = texEditPending == id2;
                                        if (ImGui::Selectable(name.c_str(), selected)) {
                                            texEditPending = id2;
                                        }
                                        if (ImGui::IsItemHovered()) {
                                            if (previewId != id2) {
                                                if (previewId.IsValid()) state->editor.texResidency.Release(previewId, ctx);
                                                previewId = id2;
                                                // todo: ideally only load the lowest mip for preview
                                                state->editor.texResidency.Acquire(id2, ctx);
                                            }
                                            ImGui::BeginTooltip();
                                            uint64_t ds = state->editor.texResidency.GetDescSet(id2, ctx);
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
                                        state->editor.texResidency.Release(previewId, ctx);
                                        previewId = Engine::TextureID::INVALID;
                                    }
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Cancel") || state->inputFrame->GetKey(Key::ESCAPE).pressed) {
                                    if (previewId.IsValid()) {
                                        state->editor.texResidency.Release(previewId, ctx);
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
                                if (aniso) {
                                    static const float anisoLevels[] = {1.0f, 2.0f, 4.0f, 8.0f, 16.0f};
                                    static const char* anisoLabels[] = {"1x", "2x", "4x", "8x", "16x"};
                                    int anisoIdx = 0;
                                    for (int k = 4; k >= 0; --k) {
                                        if (samplerEditPending.maxAnisotropy >= anisoLevels[k]) {
                                            anisoIdx = k;
                                            break;
                                        }
                                    }
                                    if (ImGui::Combo("Max Anisotropy", &anisoIdx, anisoLabels, 5)) {
                                        samplerEditPending.maxAnisotropy = anisoLevels[anisoIdx];
                                    }
                                }

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
                if (materialPendingDelete.IsValid()) {
                    materialManager->DeleteMutableMaterial(materialPendingDelete);
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Text Materials")) {
                static char newTextMatName[128] = "new_text_material";
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputText("##textmatname", newTextMatName, sizeof(newTextMatName));
                ImGui::SameLine();
                const bool textNameEmpty = newTextMatName[0] == '\0';
                const bool textNameExists = !textNameEmpty && materialManager->FindTextMaterial(StringID{newTextMatName, strlen(newTextMatName)}).IsValid();
                ImGui::BeginDisabled(textNameEmpty || textNameExists);
                if (ImGui::Button("Create Text Material")) {
                    materialManager->CreateTextMaterial(newTextMatName);
                }
                ImGui::EndDisabled();
                if (textNameExists) {
                    ImGui::SameLine();
                    ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
                }

                const auto& allTextMaterials = materialManager->GetTextMaterials();
                ImGui::SeparatorText(fmt::format("Text Materials ({})", allTextMaterials.Size()).c_str());

                static Engine::TextMaterialID textMatRenameActive = Engine::TextMaterialID::INVALID;
                static char textMatRenameBuffer[128] = {};

                Engine::TextMaterialID textMatPendingDelete = Engine::TextMaterialID::INVALID;
                for (const auto& [id, mat] : allTextMaterials) {
                    ImGui::PushID(static_cast<int>(id.id));
                    if (ImGui::CollapsingHeader(mat.name.c_str())) {
                        ImGui::BeginDisabled(true);
                        ImGui::Text("ID: %llu", id.id);
                        ImGui::EndDisabled();
                        if (ImGui::Button("Delete")) {
                            textMatPendingDelete = id;
                        }
                        ImGui::SameLine();
                        const bool isRenaming = textMatRenameActive == id;
                        if (isRenaming) {
                            ImGui::SetNextItemWidth(180.0f);
                            ImGui::InputText("##rename", textMatRenameBuffer, sizeof(textMatRenameBuffer));
                            ImGui::SameLine();
                            const bool nameUnchanged = strcmp(textMatRenameBuffer, mat.name.c_str()) == 0;
                            const bool nameEmpty = textMatRenameBuffer[0] == '\0';
                            const bool nameExists = !nameEmpty && !nameUnchanged && materialManager->FindTextMaterial(StringID{textMatRenameBuffer, strlen(textMatRenameBuffer)}).IsValid();
                            ImGui::BeginDisabled(nameEmpty || nameUnchanged || nameExists);
                            if (ImGui::Button("Apply")) {
                                materialManager->RenameTextMaterial(id, textMatRenameBuffer);
                                textMatRenameActive = Engine::TextMaterialID::INVALID;
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            if (ImGui::Button("Cancel")) {
                                textMatRenameActive = Engine::TextMaterialID::INVALID;
                            }
                            if (nameExists) {
                                ImGui::SameLine();
                                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "already exists");
                            }
                        }
                        else {
                            if (ImGui::Button("Rename")) {
                                textMatRenameActive = id;
                                const auto& n = mat.name;
                                const size_t copyLen = std::min(n.Size(), sizeof(textMatRenameBuffer) - 1);
                                memcpy(textMatRenameBuffer, n.c_str(), copyLen);
                                textMatRenameBuffer[copyLen] = '\0';
                            }
                        }

                        Engine::TextMaterial editMat = mat;
                        bool changed = false;

                        changed |= ImGui::ColorEdit4("Color Tint", &editMat.colorTint.x);

                        ImGui::SeparatorText("Outline");
                        changed |= ImGui::ColorEdit4("Outline Color", &editMat.outlineColor.x);
                        changed |= ImGui::SliderFloat("Outline Width", &editMat.outlineWidth, 0.0f, 0.475f);

                        ImGui::SeparatorText("Shadow");
                        changed |= ImGui::ColorEdit4("Shadow Color", &editMat.shadowColor.x);
                        changed |= ImGui::DragFloat2("Shadow Offset", &editMat.shadowOffset.x, 0.001f);
                        changed |= ImGui::SliderFloat("Shadow Softness", &editMat.shadowSoftness, 0.0f, 1.0f);

                        if (changed) {
                            materialManager->UpdateTextMaterial(id, editMat);
                        }
                    }
                    ImGui::PopID();
                }
                if (textMatPendingDelete.IsValid()) {
                    materialManager->DeleteTextMaterial(textMatPendingDelete);
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    if (ImGui::Begin("Textures")) {
        if (!state->editor.textureInfoCache) {
            const uint32_t count = ctx->assetManager->GetTextureInfoCount();
            state->editor.textureInfoCache = ctx->editorArena.Get().Alloc<Core::ArenaFixedMap<Engine::TextureID, Engine::AssetManager::EditorTextureInfo> >(&ctx->editorArena.Get(), count);
            ctx->assetManager->GetAllTextureInfos(*state->editor.textureInfoCache);
        }
        const uint32_t texCount = static_cast<uint32_t>(state->editor.textureInfoCache->Size());
        auto sorted = Core::ArenaFixedVector<Engine::AssetManager::EditorTextureInfo>(&ctx->editorArena.Get(), texCount);
        for (const auto& [texId, info] : *state->editor.textureInfoCache) {
            sorted.EmplaceBack(info);
        }
        std::ranges::sort(sorted, {}, &Engine::AssetManager::EditorTextureInfo::name);

        if (ImGui::BeginTable("##textures", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Width", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Height", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Mips", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            for (const auto& entry : sorted) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(entry.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", entry.width);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", entry.height);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%u", entry.mipCount);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();

    frameBuffer->mainViewFamily.shadowConfig = state->lighting.shadowConfig;
    frameBuffer->mainViewFamily.postProcessConfig = state->lighting.postProcess;
    frameBuffer->mainViewFamily.aaMode = state->lighting.aaMode;
    frameBuffer->mainViewFamily.gtaoConfig = state->lighting.gtaoConfig;
    frameBuffer->mainViewFamily.smaaConfig = state->lighting.smaaConfig;
    frameBuffer->mainViewFamily.debugResourceName = state->debug.resourceName;
    frameBuffer->mainViewFamily.debugTransformationType = state->debug.transformationType;
    frameBuffer->mainViewFamily.debugViewAspect = state->debug.viewAspect;
}
}
