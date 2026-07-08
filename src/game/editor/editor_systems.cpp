//
// Created by William on 2026-01-30.
//

#include "editor_systems.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include <tracy/Tracy.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "game/systems/debug_system.h"
#include "game/editor/settings/graphics_settings.h"
#include "game/editor/settings/input_settings.h"
#include "game/input/game_actions.h"
#include "game/editor/editor_scene_browser.h"
#include "game/editor/editor_materials.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "game/systems/scene_system.h"
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
#include "game/components/render/light_components.h"
#include "game/components/render/static_mesh_primitive_component.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/render/text3d_component.h"
#include "game/components/render/text_component.h"

namespace Game
{
static bool HandleViewportSelection(Engine::EngineContext* ctx, Engine::EngineState* state);

static void HandleEditorHotkeys(Engine::EngineContext* ctx, Engine::EngineState* state);

static void DrawGameplayWindow(Engine::EngineState* state);

static void DrawViewManipulatorAndOverlay(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);

static void DrawToolbar(Engine::EngineContext* ctx, Engine::EngineState* state);

static void DrawDetailsPanel(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, const glm::vec3& centroid, int transformCount);

static void DrawSelectionGizmos(Engine::EngineState* state, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& multiGizmoCentroid, bool bJustSelected);

static void DrawSceneStatsWindow(Engine::EngineState* state);

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

        if (allHaveName && firstName) {
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
        bool sameFolder = true;
        StringID firstFolderId;
        bool first = true;
        for (auto e : entities) {
            auto* fc = state->registry.try_get<Component::EntityFolderComponent>(e);
            if (!fc) {
                allHaveFolder = false;
                break;
            }
            if (first) {
                firstFolderId = fc->folderId;
                first = false;
            }
            else if (fc->folderId != firstFolderId) {
                sameFolder = false;
            }
        }

        if (allHaveFolder) {
            auto anchorView = state->registry.view<Component::SceneFolderComponent>();
            const char* display = "...";
            if (sameFolder) {
                display = "(None)";
                if (firstFolderId.IsValid()) {
                    for (auto a : anchorView) {
                        if (anchorView.get<Component::SceneFolderComponent>(a).folderId == firstFolderId) {
                            display = anchorView.get<Component::SceneFolderComponent>(a).name.c_str();
                            break;
                        }
                    }
                }
            }

            ImGui::Text("Folder");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##multi_folder", display)) {
                if (ImGui::Selectable("(None)", sameFolder && !firstFolderId.IsValid())) {
                    for (auto e : entities) {
                        state->registry.get_or_emplace<Component::EntityFolderComponent>(e).folderId = StringID();
                    }
                    MarkEntitiesModified(state, entities);
                }
                for (auto a : anchorView) {
                    const auto& fc = anchorView.get<Component::SceneFolderComponent>(a);
                    if (const auto* sc = state->registry.try_get<Component::SceneComponent>(a); sc && sc->sceneId != state->currentSceneId) {
                        continue;
                    }
                    Core::ShortString label;
                    if (fc.parentFolder.IsValid()) { label.Append("    "); }
                    label.Append(fc.name);
                    if (ImGui::Selectable(label.c_str(), sameFolder && firstFolderId == fc.folderId)) {
                        for (auto e : entities) {
                            state->registry.get_or_emplace<Component::EntityFolderComponent>(e).folderId = fc.folderId;
                        }
                        MarkEntitiesModified(state, entities);
                    }
                }
                ImGui::EndCombo();
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

    if (state->inputContext != Engine::InputContext::Editor) {
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (!popupOpen && state->input.GetActionState(Actions::ACTION_ESCAPE).pressed) {
            if (state->inputContext == Engine::InputContext::Gameplay) {
                state->inputContext = Engine::InputContext::Menu;
                ctx->setCursorHiddenFn(false);

                // Snap the editor camera to the game camera
                auto gameCamView = state->registry.view<Component::GameCameraTag, Component::TransformComponent>();
                auto editorCamView = state->registry.view<Component::EditorCameraTag, Component::TransformComponent>();
                if (gameCamView.front() != entt::null && editorCamView.front() != entt::null) {
                    const auto& gameT = gameCamView.get<Component::TransformComponent>(gameCamView.front());
                    auto& editorT = editorCamView.get<Component::TransformComponent>(editorCamView.front());
                    editorT.translation = gameT.translation;
                    editorT.rotation = gameT.rotation;
                }
            }
            else {
                PlayStop(ctx, state);
            }
        }
    }
}

void EditorTickInput(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->inputContext != Engine::InputContext::Gameplay && !ctx->bImguiMouseCaptured && !state->editor.bExclusiveGizmoActivePrev && state->input.GetActionState(Actions::ACTION_VIEWPORT_SELECT).pressed) {
        state->bViewportClickPending = true;
    }
    HandleEditorHotkeys(ctx, state);
}

void DrawEditorInterface(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
    state->editor.texResidency.Tick(ctx);
    state->editor.ResetFrameCache();
    state->editor.bExclusiveGizmoActivePrev = state->editor.bExclusiveGizmoActive;
    state->editor.bExclusiveGizmoActive = false;

    const bool bJustSelected = HandleViewportSelection(ctx, state);

    DrawDebugViewWindow(ctx, state);
    DrawProjectConfigWindow(ctx, state);
    DrawLightingWindow(ctx, state);
    DrawInputBindingsWindow(ctx, state);
    DrawGameplayWindow(state);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(
        static_cast<float>(ctx->windowContext.viewportOffsetX),
        static_cast<float>(ctx->windowContext.viewportOffsetY),
        static_cast<float>(ctx->windowContext.viewportWidth),
        static_cast<float>(ctx->windowContext.viewportHeight)
    );

    DrawViewManipulatorAndOverlay(ctx, state, frameBuffer);

    const glm::mat4 view = frameBuffer->mainViewFamily.mainView.currentViewData.view;
    const glm::mat4 proj = frameBuffer->mainViewFamily.mainView.currentViewData.proj;

    DrawToolbar(ctx, state);
    DrawSceneBrowser(ctx, state, frameBuffer);

    glm::vec3 multiGizmoCentroid{0.0f};
    int transformCount = 0;
    for (auto entity : state->editor.selectedEntities) {
        if (state->registry.all_of<Component::TransformComponent>(entity)) {
            multiGizmoCentroid += Component::ComputeWorldTransform(state->registry, entity).translation;
            ++transformCount;
        }
    }
    if (transformCount > 0) {
        multiGizmoCentroid /= static_cast<float>(transformCount);
    }

    DrawDetailsPanel(ctx, state, frameBuffer, multiGizmoCentroid, transformCount);
    DrawSelectionGizmos(state, view, proj, multiGizmoCentroid, bJustSelected);

    DrawPostProcessingWindow(state);
    DrawSceneStatsWindow(state);
    DrawMaterialsWindow(ctx, state);
    DrawTexturesWindow(ctx, state);

    frameBuffer->mainViewFamily.debugResourceName = state->debug.resourceName;
    frameBuffer->mainViewFamily.debugTransformationType = state->debug.transformationType;
    frameBuffer->mainViewFamily.debugViewAspect = state->debug.viewAspect;
}

static bool HandleViewportSelection(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    bool bJustSelected = false;

    const bool ctrlHeld = state->input.GetActionState(Actions::ACTION_MODIFIER_CTRL).down;
    if (state->bViewportClickPending) {
        state->bViewportClickPending = false;

        if (state->inputContext != Engine::InputContext::Gameplay && !ctx->bImguiMouseCaptured && !state->editor.bExclusiveGizmoActivePrev) {
            auto it = state->stableIdToEntityMap.Find(StringID{ctx->lastKnownStableIdUnderCursor});
            if (it != nullptr) {
                bJustSelected = true;
                state->editor.selectedFolders.Clear();
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
                state->editor.selectedFolders.Clear();
                state->editor.selectedEntities.Clear();
            }
        }
    }

    return bJustSelected;
}

static void ToggleDebugView(Engine::EngineState* state, const DebugHotkey& hotkey)
{
    if (state->debug.resourceName == hotkey.resourceName && state->debug.viewAspect == hotkey.aspect && state->debug.transformationType == hotkey.transform) {
        state->debug.resourceName.Clear();
    }
    else {
        state->debug.resourceName = Core::InlineString(hotkey.resourceName);
        state->debug.transformationType = hotkey.transform;
        state->debug.viewAspect = hotkey.aspect;
    }
}

static void HandleEditorHotkeys(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    const bool ctrlHeld = state->input.GetActionState(Actions::ACTION_MODIFIER_CTRL).down;

    if (state->inputContext == Engine::InputContext::Editor && !ctx->bImGuiWantsTextInput) {
        const bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        const bool rmbHeld = state->input.GetActionState(Actions::ACTION_EDITOR_CAM_LOOK_MODIFIER).down;
        const bool multiSelectActive = state->editor.selectedEntities.Size() > 1;

        if (multiSelectActive && state->editor.currentGizmoOperation == ImGuizmo::SCALE) {
            state->editor.currentGizmoOperation = ImGuizmo::TRANSLATE;
        }
        if (!ctrlHeld) {
            if (state->input.GetActionState(Actions::ACTION_GIZMO_TRANSLATE).pressed) {
                state->editor.currentGizmoOperation = ImGuizmo::TRANSLATE;
            }
            else if (state->input.GetActionState(Actions::ACTION_GIZMO_ROTATE).pressed) {
                state->editor.currentGizmoOperation = ImGuizmo::ROTATE;
            }
            else if (state->input.GetActionState(Actions::ACTION_GIZMO_SCALE).pressed && !multiSelectActive) {
                state->editor.currentGizmoOperation = ImGuizmo::SCALE;
            }
        }

        if (!rmbHeld) {
            if (!popupOpen && ctrlHeld && state->input.GetActionState(Actions::ACTION_DUPLICATE).pressed) {
                auto copies = Core::ArenaFixedVector<entt::entity>(&ctx->editorArena.Get(), state->editor.selectedEntities.Size());
                for (entt::entity entity : state->editor.selectedEntities) {
                    if (!state->registry.valid(entity)) continue;
                    entt::entity copy = CopySceneEntity(state, entity, state->currentSceneId);
                    if (auto* nameComp = state->registry.try_get<Component::NameComponent>(copy)) {
                        nameComp->name = GenerateIncrementedName(state->registry, state->currentSceneId, nameComp->name);
                    }
                    state->registry.get<Component::StableIdComponent>(copy).sortOrder = HighestSortOrderInScene(state->registry, state->currentSceneId) + 1;
                    copies.PushBack(copy);
                }
                state->editor.selectedEntities.Clear();
                for (auto copy : copies) { state->editor.selectedEntities.PushBack(copy); }
                if (!copies.IsEmpty()) { MarkSceneModified(state, state->currentSceneId); }
            }

            if (!popupOpen && state->input.GetActionState(Actions::ACTION_DELETE_SELECTED).pressed) {
                const bool hadSelection = !state->editor.selectedEntities.IsEmpty();
                for (entt::entity entity : state->editor.selectedEntities) {
                    if (!state->registry.valid(entity)) continue;
                    state->registry.destroy(entity);
                }
                state->editor.selectedEntities.Clear();
                if (hadSelection) { MarkSceneModified(state, state->currentSceneId); }
            }

            if (!popupOpen && state->input.GetActionState(Actions::ACTION_ESCAPE).pressed) {
                state->editor.selectedFolders.Clear();
                state->editor.selectedEntities.Clear();
            }

            if (!popupOpen && state->input.GetActionState(Actions::ACTION_BEGIN_RENAME).pressed) {
                if (state->editor.selectedFolders.Size() == 1 && state->registry.valid(state->editor.selectedFolders[0])) {
                    if (const auto* fc = state->registry.try_get<Component::SceneFolderComponent>(state->editor.selectedFolders[0])) {
                        state->editor.renamingEntity = state->editor.selectedFolders[0];
                        state->editor.renameRequestFocus = true;
                        strncpy_s(state->editor.renameBuffer, fc->name.c_str(), sizeof(state->editor.renameBuffer) - 1);
                    }
                }
                else if (state->editor.selectedEntities.Size() == 1) {
                    entt::entity target = state->editor.selectedEntities[0];
                    if (state->registry.valid(target)) {
                        state->editor.renamingEntity = target;
                        state->editor.renameRequestFocus = true;
                        const auto* nc = state->registry.try_get<Component::NameComponent>(target);
                        strncpy_s(state->editor.renameBuffer, nc ? nc->name.c_str() : "", sizeof(state->editor.renameBuffer) - 1);
                    }
                }
            }

            if (!popupOpen && state->input.GetActionState(Actions::ACTION_FOCUS_SELECTION).pressed && !state->editor.selectedEntities.IsEmpty()) {
                entt::entity target = state->editor.selectedEntities.Front();
                if (state->registry.valid(target)) {
                    auto* targetTransform = state->registry.try_get<Component::TransformComponent>(target);

                    auto editorCamView = state->registry.view<Component::EditorCameraTag, Component::FreeCameraComponent, Component::TransformComponent>();
                    for (entt::entity camEntity : editorCamView) {
                        auto& camTransform = editorCamView.get<Component::TransformComponent>(camEntity);
                        const glm::vec3 camForward = glm::normalize(camTransform.rotation * WORLD_FORWARD);

                        glm::vec3 focusPoint = targetTransform ? Component::ComputeWorldTransform(state->registry, target).translation : glm::vec3(0.0f);
                        float focusDist = 1.0f;

                        Engine::StaticModelHandle modelHandle = Engine::StaticModelHandle::INVALID;
                        if (const auto* rt = state->registry.try_get<Component::MeshRuntime>(target)) {
                            modelHandle = rt->modelHandle;
                        }

                        if (modelHandle.IsValid()) {
                            if (const Engine::StaticModel* model = ctx->assetManager->GetModel(modelHandle)) {
                                if (model->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded) {
                                    Engine::AABB aabb = model->bounds.aabb;
                                    // Single primitive instead of whole model
                                    if (const auto* prim = state->registry.try_get<Component::StaticMeshPrimitiveComponent>(target)) {
                                        uint32_t ordinal = 0;
                                        const auto& nodes = model->modelData.nodes;
                                        const auto& meshes = model->modelData.meshes;
                                        for (uint32_t n = 0; n < nodes.Size(); ++n) {
                                            const uint32_t mi = nodes[n].meshIndex;
                                            if (mi == ~0u || mi >= meshes.Size()) { continue; }
                                            const auto& props = meshes[mi].primitiveProperties;
                                            if (prim->primitiveOrdinal >= ordinal && prim->primitiveOrdinal < ordinal + props.Size()) {
                                                const Engine::PrimitiveProperty& p = props[prim->primitiveOrdinal - ordinal];
                                                aabb = Engine::AABB{p.boundingBoxMin, p.boundingBoxMax};
                                                break;
                                            }
                                            ordinal += static_cast<uint32_t>(props.Size());
                                        }
                                    }
                                    const glm::vec3 localCenter = aabb.Center();
                                    const glm::vec3 localHalf = aabb.HalfExtents();

                                    if (targetTransform) {
                                        const Transform world = Component::ComputeWorldTransform(state->registry, target);
                                        const glm::mat4 worldMatrix = world.GetMatrix();
                                        focusPoint = glm::vec3(worldMatrix * glm::vec4(localCenter, 1.0f));
                                        const glm::vec3 scale = world.scale;
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

        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_1).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[0]); }
        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_2).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[1]); }
        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_3).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[2]); }
        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_4).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[3]); }
        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_5).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[4]); }
        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_6).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[5]); }
        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_7).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[6]); }
        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_8).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[7]); }
        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_9).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[8]); }
        if (state->input.GetActionState(Actions::ACTION_DEBUG_VIEW_0).pressed) { ToggleDebugView(state, DEBUG_HOTKEYS[9]); }
    }
}

static void DrawGameplayWindow(Engine::EngineState* state)
{
    if (ImGui::Begin("Gameplay")) {
        if (state->inputContext != Engine::InputContext::Editor) {
            ImGui::Text("Checkpoint ID:       %llu", state->currentCheckpointId.id);
            ImGui::Text("Checkpoint Priority: %d", state->currentCheckpointPriority);
        }
        else {
            ImGui::TextDisabled("Not playing");
        }
    }
    ImGui::End();
}

static void DrawViewManipulatorAndOverlay(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    auto editorCameraView = state->registry.view<Component::FreeCameraComponent, Component::TransformComponent, Component::EditorCameraTag>();
    assert(editorCameraView.size_hint() > 0);
    auto& editorCameraTransform = editorCameraView.get<Component::TransformComponent>(editorCameraView.front());

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

        constexpr ImGuiWindowFlags fpsFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize;
        const auto vpLeft = static_cast<float>(ctx->windowContext.viewportOffsetX);
        ImGui::SetNextWindowPos(ImVec2(vpLeft + 8.0f, vpTop + 8.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.5f);
        if (ImGui::Begin("##fps_overlay", nullptr, fpsFlags)) {
            const float fps = frameBuffer->timeFrame.renderFps;
            ImGui::Text("%.0f FPS (%.2f ms)", fps, fps > 0.0f ? 1000.0f / fps : 0.0f);
        }
        ImGui::End();
    }
}

static void DrawToolbar(Engine::EngineContext* ctx, Engine::EngineState* state)
{
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

        if (state->inputContext != Engine::InputContext::Editor) {
            if (ImGui::Button("Stop")) {
                PlayStop(ctx, state);
            }
            if (state->inputContext != Engine::InputContext::Gameplay) {
                ImGui::SameLine();
                if (ImGui::Button("Enter Game")) {
                    state->inputContext = Engine::InputContext::Gameplay;
                    ctx->setCursorHiddenFn(true);
                    state->editor.selectedEntities.Clear();
                }
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
            static constexpr const char* kPhysicsDebugLabels[] = {"Off", "Sensor Only", "Sensor + Tag", "On", "Selected"};
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
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Physics debug draw: Off / Sensor Only / Sensor + Tag / On (all) / Selected"); }
        }
    }
    ImGui::End();
}

static void DrawDetailsPanel(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, const glm::vec3& centroid, int transformCount)
{
    if (ImGui::Begin("Details")) {
        ImGui::Checkbox("Expose all components", &state->editor.bExposeAllComponents);
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Show engine-managed components (read-only)"); }
        ImGui::Separator();

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
                if (entry.hideInInspector && !state->editor.bExposeAllComponents) { continue; }
                if (entry.has(state->registry, entity)) {
                    const bool readOnly = entry.hideInInspector;

                    nlohmann::json before;
                    entry.serialize(state->registry, entity, before);

                    if (readOnly) { ImGui::BeginDisabled(true); }
                    Engine::ComponentEditorResult result = entry.drawEditor(frameBuffer->mainViewFamily, state->registry, entity, entry.name);
                    if (readOnly) { ImGui::EndDisabled(); }

                    if (result.requestRemoval && !entry.hidden) {
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
                    if (entry.hidden) { continue; }
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
        else if (state->editor.selectedEntities.Size() > 1) {
            DrawMultiSelectEditor(ctx, state, centroid, transformCount);
        }
    }
    ImGui::End();
}

static void DrawSelectionGizmos(Engine::EngineState* state, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& multiGizmoCentroid, bool bJustSelected)
{
    if (!state->editor.bExclusiveGizmoActive && !bJustSelected && !state->editor.selectedEntities.IsEmpty()) {
        if (state->editor.selectedEntities.Size() == 1) {
            entt::entity entity = state->editor.selectedEntities[0];
            if (auto* transform = state->registry.try_get<Component::TransformComponent>(entity)) {
                glm::mat4 parentWorld(1.0f);
                if (auto* h = state->registry.try_get<Component::HierarchyComponent>(entity); h && state->registry.valid(h->parent)) {
                    parentWorld = Component::ComputeWorldTransform(state->registry, h->parent).GetMatrix();
                }
                glm::mat4 model = parentWorld * Component::GetMatrix(*transform);
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
                    const glm::mat4 localModel = glm::inverse(parentWorld) * model;
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localModel), t, r, s);
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

                    Transform world = Component::ComputeWorldTransform(state->registry, entity);
                    world.translation += deltaTranslation;
                    const glm::vec3 rel = world.translation - multiGizmoCentroid;
                    world.translation = multiGizmoCentroid + deltaRotation * rel;
                    world.rotation = glm::normalize(deltaRotation * world.rotation);

                    Transform parentWorld = Transform::IDENTITY;
                    if (auto* h = state->registry.try_get<Component::HierarchyComponent>(entity); h && state->registry.valid(h->parent)) {
                        parentWorld = Component::ComputeWorldTransform(state->registry, h->parent);
                    }
                    const Transform local = Component::ComposeLocalFromWorld(parentWorld, world);
                    transform->translation = local.translation;
                    transform->rotation = local.rotation;

                    state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
                    if (state->inputContext != Engine::InputContext::Editor) {
                        state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(entity);
                    }
                }
            }
            else {
                s_wasDragging = false;
                s_prevTranslation = {};
            }
        }
    }
}

static void DrawSceneStatsWindow(Engine::EngineState* state)
{
    if (ImGui::Begin("Scene")) {
        ImGui::Checkbox("Enable Physics", &state->physics.bEnabled);

        ImGui::SeparatorText("Meshes");
        ImGui::Text("Static:     %zu", state->registry.view<Component::StaticMeshComponent>().size());
        ImGui::Text("Procedural: %zu", state->registry.view<Component::ProceduralMeshComponent>().size());
        ImGui::Text("Spline:     %zu", state->registry.view<Component::SplineMeshComponent>().size());
        ImGui::Text("Text:       %zu", state->registry.view<Component::TextComponent>().size());
        ImGui::Text("Text3D:     %zu", state->registry.view<Component::Text3DComponent>().size());

        ImGui::SeparatorText("Lights");
        ImGui::Text("Area:   %zu", state->registry.view<Component::AreaLightComponent>().size());
        ImGui::Text("Sphere: %zu", state->registry.view<Component::SphereLightComponent>().size());
    }
    ImGui::End();
}
}
