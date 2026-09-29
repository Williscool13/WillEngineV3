//
// Created by William on 2026-09-29.
//

#include "editor_scene_panels.h"

#include <algorithm>
#include <cstring>

#include "imgui.h"
#include "engine/editor/editor_systems.h"
#include "engine/systems/scene_system.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/asset_manager.h"
#include "engine/core/model_id.h"
#include "core/containers/arena_fixed_vector.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/scene_components.h"

namespace Engine
{
static void SelectOnly(Engine::EngineState* state, entt::entity entity)
{
    state->editor.selectedFolders.Clear();
    state->editor.selectedEntities.Clear();
    state->editor.selectedEntities.PushBack(entity);
}

void DrawSpawnPanel(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    bool& bOpen = state->editor.windowOpen[EDITOR_WINDOW_SPAWN];
    if (!bOpen) { return; }
    if (ImGui::Begin(EDITOR_WINDOWS[EDITOR_WINDOW_SPAWN].title, &bOpen)) {
        const auto& viewData = frameBuffer->mainViewFamily.mainView.currentViewData;
        const glm::vec3 spawnPos = viewData.cameraPos + normalize(viewData.cameraForward) * 5.0f;
        const bool bHasScene = state->scene.currentSceneId.IsValid();
        if (!bHasScene) {
            ImGui::TextDisabled("No active scene");
        }
        ImGui::BeginDisabled(!bHasScene);

        if (ImGui::Button("Create Entity")) {
            const entt::entity newEntity = CreateSceneEntity(state);
            state->registry.get<Component::TransformComponent>(newEntity).translation = spawnPos;
            SelectOnly(state, newEntity);
            MarkSceneModified(state, state->scene.currentSceneId);
        }

        ImGui::SeparatorText("Model");

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
        if (ImGui::Button("Spawn Model")) {
            auto spawned = SpawnModel(ctx, state, modelList[selectedModel].id, spawnPos);
            if (!spawned.IsEmpty()) {
                state->editor.selectedFolders.Clear();
                state->editor.selectedEntities.Clear();
                for (auto entity : spawned) {
                    state->editor.selectedEntities.PushBack(entity);
                }
                MarkSceneModified(state, state->scene.currentSceneId);
            }
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText("Prefab");

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
        ImGui::BeginDisabled((isExistingPrefab && !isMasterPrefab) || !ctx->bGameLoaded);
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
            entt::entity spawned = SpawnPrefab(state, ctx->assetManager, prefabList[selectedPrefab].id, spawnPos);
            if (spawned != entt::null) {
                SelectOnly(state, spawned);
                MarkSceneModified(state, state->scene.currentSceneId);
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

        ImGui::EndDisabled();
    }
    ImGui::End();
}
}
