//
// Created by William on 2026-06-26.
//

#include "editor_scene_browser.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <fmt/format.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "game/editor/editor_systems.h"
#include "game/systems/scene_system.h"
#include "game/input/game_actions.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/asset_manager.h"
#include "engine/core/model_id.h"
#include "core/input/input_frame.h"
#include "core/containers/arena_array.h"
#include "core/containers/arena_fixed_vector.h"
#include "game/fwd_components.h"
#include "game/components/common_components.h"
#include "game/components/editor_components.h"
#include "game/components/scene_components.h"

namespace Game
{
void DrawSceneBrowser(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    const glm::vec3 cameraPos = frameBuffer->mainViewFamily.mainView.currentViewData.cameraPos;
    const glm::vec3 cameraFwd = frameBuffer->mainViewFamily.mainView.currentViewData.cameraForward;

    if (state->editor.renamingEntity != entt::null && !state->registry.valid(state->editor.renamingEntity)) {
        state->editor.renamingEntity = entt::null;
    }
    for (int i = static_cast<int>(state->editor.selectedFolders.Size()) - 1; i >= 0; --i) {
        if (!state->registry.valid(state->editor.selectedFolders[i])) {
            state->editor.selectedFolders.Remove(state->editor.selectedFolders.begin() + i);
        }
    }

    if (ImGui::Begin("Scene Browser")) {
        const auto& sceneCache = ctx->assetManager->GetSceneCache();

        if (!sceneCache.IsEmpty() && !sceneCache.Contains(state->scene.currentSceneId)) {
            for (const auto& [id, meta] : sceneCache) {
                state->scene.currentSceneId = id;
                state->scene.currentSceneName = meta.sceneName;
                break;
            }
        }
        if (sceneCache.IsEmpty()) {
            state->scene.currentSceneId = {};
            state->scene.currentSceneName.Clear();
        }

        const bool bIsLoaded = std::ranges::any_of(state->editor.loadedScenes, [&](const auto& m) { return m.sceneId == state->scene.currentSceneId; });
        const bool bIsModified = std::ranges::find(state->editor.modifiedScenes, state->scene.currentSceneId) != state->editor.modifiedScenes.end();
        const bool bIsMaxLoaded = state->editor.loadedScenes.Size() > Engine::MAX_LOADED_SCENES;
        const bool hasScene = sceneCache.Contains(state->scene.currentSceneId);

        // Scene dropdown
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##scene_list", state->scene.currentSceneName.c_str())) {
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
                const bool selected = (id == state->scene.currentSceneId);
                if (ImGui::Selectable(name.c_str(), selected)) {
                    state->scene.currentSceneId = id;
                    state->scene.currentSceneName = name;
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
            LoadSceneFromFile(state, ctx->assetManager, state->scene.currentSceneId);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!bIsLoaded);
        if (ImGui::Button("Unload")) { UnloadScene(state, state->scene.currentSceneId); }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!bIsLoaded);
        if (bIsModified) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 1.0f)); }
        if (ImGui::Button(bIsModified ? "Save*" : "Save")) {
            SaveSceneToFile(state->scene.currentSceneId, state->scene.currentSceneName.View(), state, ctx->assetManager, ctx);
            state->editor.modifiedScenes.RemoveFirst(state->scene.currentSceneId);
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
            ctx->assetManager->DeleteScene(state->scene.currentSceneId);
            state->scene.currentSceneId = {};
            state->scene.currentSceneName.Clear();
        }
        ImGui::EndDisabled();
        if (bIsLoaded && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Unload scene before deleting");
        }

        ImGui::TextDisabled("ID: %llu", state->scene.currentSceneId.id);

        ImGui::BeginDisabled(!hasScene);
        if (ImGui::Button("Set Default")) {
            state->projectConfig.defaultScene = Core::InlineString<256>(state->scene.currentSceneName.View());
            Engine::WriteProjectConfig(state->projectConfig, state->allocator);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && hasScene) {
            ImGui::SetTooltip("Set '%s' as the scene loaded on startup (non-editor)", state->scene.currentSceneName.c_str());
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
            state->scene.currentSceneId = newId;
            state->scene.currentSceneName = Core::InlineString<128>(newSceneName);
            state->editor.loadedScenes.PushBack({newId});
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
                state->editor.selectedFolders.Clear();
                state->editor.selectedEntities.Clear();
                for (auto entity : spawned) {
                    state->editor.selectedEntities.PushBack(entity);
                }
                MarkSceneModified(state, state->scene.currentSceneId);
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
                state->editor.selectedFolders.Clear();
                state->editor.selectedEntities.Clear();
                state->editor.selectedEntities.PushBack(spawned);
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

        ImGui::NewLine();

        ImGui::SeparatorText("Entities");
        if (ImGui::Button("Create Entity")) {
            auto newEntity = CreateSceneEntity(state);
            const auto& viewData = frameBuffer->mainViewFamily.mainView.currentViewData;
            state->registry.get<Component::TransformComponent>(newEntity).translation = viewData.cameraPos + viewData.cameraForward * 5.0f;
            state->editor.selectedFolders.Clear();
            state->editor.selectedEntities.Clear();
            state->editor.selectedEntities.PushBack(newEntity);
            MarkSceneModified(state, state->scene.currentSceneId);
        }
        ImGui::SameLine();
        if (ImGui::Button("New Folder")) {
            entt::entity f = state->registry.create();
            state->registry.emplace<Component::SceneComponent>(f, state->scene.currentSceneId);
            Component::SceneFolderComponent folder{};
            folder.folderId = StringID{state->rng()};
            folder.name = Core::ShortString("New Folder");
            state->registry.emplace<Component::SceneFolderComponent>(f, std::move(folder));
            MarkSceneModified(state, state->scene.currentSceneId);
        }
        char* search = state->editor.sceneBrowserSearch;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##search", search, sizeof(state->editor.sceneBrowserSearch));

        // Component filter
        StringID& componentFilterId = state->editor.sceneBrowserComponentFilter;
        const Engine::ComponentEntry* componentFilter = nullptr;
        if (componentFilterId.IsValid()) {
            if (const auto* idx = state->componentRegistry.registryMapping.Find(componentFilterId)) {
                componentFilter = &state->componentRegistry.registry[*idx];
            }
            else {
                componentFilterId = {};
            }
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##component_filter", componentFilter ? componentFilter->name : "All Components")) {
            if (ImGui::Selectable("All Components", componentFilter == nullptr)) {
                componentFilterId = {};
            }
            for (const auto& entry : state->componentRegistry.registry) {
                if (entry.hidden) { continue; }
                if (ImGui::Selectable(entry.name, componentFilterId == entry.typeId)) {
                    componentFilterId = entry.typeId;
                }
            }
            ImGui::EndCombo();
        }
        const bool filterActive = search[0] != '\0' || componentFilter != nullptr;

        // Collect entities
        struct EntityEntry
        {
            entt::entity entity;
            const char* label;
            uint64_t stableId;
            uint64_t sortOrder;
            StringID folderId;
            entt::entity parentEntity; // entt::null if a hierarchy root
            uint16_t depth; // (0 = root)
        };

        constexpr size_t MAX_BROWSER_ENTRIES = 4096;

        Core::ArenaVector<EntityEntry> entries{&ctx->editorArena.Get(), MAX_BROWSER_ENTRIES + 1};

        size_t totalInScene = 0;
        bool bEntriesTruncated = false;
        auto view2 = state->registry.view<Component::SceneComponent>();
        for (auto entity : view2) {
            auto& scene = view2.get<Component::SceneComponent>(entity);
            if (scene.sceneId != state->scene.currentSceneId) continue;
            if (state->registry.all_of<Component::SceneFolderComponent>(entity)) continue;
            ++totalInScene;

            if (componentFilter && !componentFilter->has(state->registry, entity)) continue;

            const char* label = "Unnamed";
            const auto* nameComp = state->registry.try_get<Component::NameComponent>(entity);
            if (nameComp) { label = nameComp->name.c_str(); }

            if (search[0]) {
                const bool nameMatches = nameComp
                                             ? nameComp->name.Contains(search, Core::CaseSensitivity::Insensitive)
                                             : Core::InlineString<16>("Unnamed").Contains(search, Core::CaseSensitivity::Insensitive);
                if (!nameMatches) { continue; }
            }

            auto* stable = state->registry.try_get<Component::StableIdComponent>(entity);
            uint64_t stableId = stable ? stable->id.id : static_cast<uint64_t>(entity);
            uint64_t sortOrder = stable ? stable->sortOrder : 0;

            StringID folderId;
            if (auto* fc = state->registry.try_get<Component::EntityFolderComponent>(entity)) {
                folderId = fc->folderId;
            }
            entt::entity parentEntity = entt::null;
            uint16_t depth = 0;
            if (auto* h = state->registry.try_get<Component::HierarchyComponent>(entity); h && state->registry.valid(h->parent)) {
                const auto* ps = state->registry.try_get<Component::SceneComponent>(h->parent);
                if (ps && ps->sceneId == state->scene.currentSceneId) {
                    parentEntity = h->parent;
                    depth = h->depth;
                }
            }
            if (entries.Size() >= MAX_BROWSER_ENTRIES) {
                bEntriesTruncated = true;
                continue;
            }
            entries.PushBack({entity, label, stableId, sortOrder, folderId, parentEntity, depth});
        }
        std::ranges::sort(entries, [](const EntityEntry& a, const EntityEntry& b) { return a.sortOrder < b.sortOrder; });

        entt::entity& s_selectionAnchor = state->editor.sceneBrowserSelectionAnchor;

        // Deferred ops
        entt::entity reorderDragged = entt::null;
        entt::entity reorderTarget = entt::null;
        bool reorderBelow = false;
        entt::entity parentDragged = entt::null;
        entt::entity parentTarget = entt::null;
        entt::entity moveToFolderEntity = entt::null;
        StringID moveToFolderId{};
        entt::entity folderToDelete = entt::null;
        StringID newSubfolderParent{};
        entt::entity reparentFolderEntity = entt::null;
        StringID reparentFolderTo{};

        Core::ArenaVector<EntityEntry*> childIndex{&ctx->editorArena.Get(), entries.Size() + 1};
        for (auto& en : entries) {
            if (en.parentEntity != entt::null) { childIndex.PushBack(&en); }
        }
        std::stable_sort(childIndex.begin(), childIndex.end(), [](const EntityEntry* a, const EntityEntry* b) {
            return static_cast<uint32_t>(a->parentEntity) < static_cast<uint32_t>(b->parentEntity);
        });

        auto collectChildren = [&](entt::entity parent) -> Core::Span<EntityEntry*> {
            const auto key = static_cast<uint32_t>(parent);
            size_t lo = 0;
            size_t hi = childIndex.Size();
            while (lo < hi) {
                const size_t mid = lo + (hi - lo) / 2;
                if (static_cast<uint32_t>(childIndex[mid]->parentEntity) < key) { lo = mid + 1; }
                else { hi = mid; }
            }
            size_t last = lo;
            while (last < childIndex.Size() && static_cast<uint32_t>(childIndex[last]->parentEntity) == key) { ++last; }
            return {childIndex.Data() + lo, last - lo};
        };

        // True if `ancestor` lies on `node`'s parent chain (so parenting node under ancestor would form a cycle).
        auto isAncestorOf = [&](entt::entity ancestor, entt::entity node) {
            entt::entity e = node;
            for (int guard = 0; e != entt::null && guard < 1024; ++guard) {
                const auto* h = state->registry.try_get<Component::HierarchyComponent>(e);
                e = (h && state->registry.valid(h->parent)) ? h->parent : entt::null;
                if (e == ancestor) { return true; }
            }
            return false;
        };

        // Entity row. Returns true if the row is an expanded parent (caller should recurse into children).
        auto drawEntityRow = [&](const EntityEntry& e, const EntityEntry* prev, const EntityEntry* next, bool hasChildren) -> bool {
            ImGui::PushID(static_cast<int>(entt::to_integral(e.entity)));
            ImGui::BeginDisabled(prev == nullptr);
            if (ImGui::SmallButton("^")) {
                std::swap(state->registry.get<Component::StableIdComponent>(e.entity).sortOrder,
                          state->registry.get<Component::StableIdComponent>(prev->entity).sortOrder);
                MarkSceneModified(state, state->scene.currentSceneId);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(next == nullptr);
            if (ImGui::SmallButton("v")) {
                std::swap(state->registry.get<Component::StableIdComponent>(e.entity).sortOrder,
                          state->registry.get<Component::StableIdComponent>(next->entity).sortOrder);
                MarkSceneModified(state, state->scene.currentSceneId);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();

            // Expand/collapse arrow for entities with transform children (state persists per-entity via ImGui storage, default open).
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID openId = ImGui::GetID("hierarchy_open");
            bool open = storage->GetInt(openId, 1) != 0;
            if (hasChildren) {
                if (ImGui::ArrowButton("expand", open ? ImGuiDir_Down : ImGuiDir_Right)) {
                    open = !open;
                    storage->SetInt(openId, open ? 1 : 0);
                }
            }
            else {
                ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), 0.0f));
                open = false;
            }
            ImGui::SameLine();

            const auto* prefabInst2 = state->registry.try_get<Component::PrefabInstanceComponent>(e.entity);
            const bool isPrefab = prefabInst2 != nullptr;
            const bool isMasterPrefab2 = isPrefab && prefabInst2->bMasterPrefab;

            if (isPrefab) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));

            if (state->editor.renamingEntity == e.entity) {
                if (state->editor.renameRequestFocus) {
                    ImGui::SetKeyboardFocusHere();
                    state->editor.renameRequestFocus = false;
                }
                ImGui::SetNextItemWidth(-1);
                const bool committed = ImGui::InputText("##rename_row", state->editor.renameBuffer, sizeof(state->editor.renameBuffer),
                                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (committed || ImGui::IsItemDeactivated()) {
                    auto& nc = state->registry.get_or_emplace<Component::NameComponent>(e.entity);
                    if (strcmp(nc.name.c_str(), state->editor.renameBuffer) != 0) {
                        nc.name = Core::InlineString<128>(state->editor.renameBuffer);
                        MarkSceneModified(state, state->scene.currentSceneId);
                    }
                    state->editor.renamingEntity = entt::null;
                }
            }
            else {
                bool selected = std::find(state->editor.selectedEntities.begin(), state->editor.selectedEntities.end(), e.entity) != state->editor.selectedEntities.end();
                char uniqueLabel[256];
                if (isMasterPrefab2) {
                    snprintf(uniqueLabel, sizeof(uniqueLabel), "[M] %s##sel", e.label);
                }
                else {
                    snprintf(uniqueLabel, sizeof(uniqueLabel), "%s##sel", e.label);
                }
                if (ImGui::Selectable(uniqueLabel, selected)) {
                    state->editor.selectedFolders.Clear();
                    const bool ctrlHeld = state->input.GetActionState(Actions::ACTION_MODIFIER_CTRL).down;
                    const bool shiftHeld = state->input.GetActionState(Actions::ACTION_MODIFIER_SHIFT).down;
                    if (shiftHeld && s_selectionAnchor != entt::null) {
                        int anchorIdx = -1;
                        int clickedIdx = -1;
                        for (int i = 0; i < static_cast<int>(entries.Size()); ++i) {
                            if (entries[i].entity == s_selectionAnchor) { anchorIdx = i; }
                            if (entries[i].entity == e.entity) { clickedIdx = i; }
                        }
                        // Range-select stays within the anchor's peer group: same folder and hierarchy depth.
                        const bool samePeerGroup = anchorIdx >= 0 && entries[anchorIdx].folderId == e.folderId && entries[anchorIdx].depth == e.depth;
                        if (anchorIdx >= 0 && clickedIdx >= 0 && samePeerGroup) {
                            if (anchorIdx > clickedIdx) { std::swap(anchorIdx, clickedIdx); }
                            if (!ctrlHeld) { state->editor.selectedEntities.Clear(); }
                            for (int i = anchorIdx; i <= clickedIdx; ++i) {
                                if (entries[i].folderId != e.folderId || entries[i].depth != e.depth) { continue; }
                                if (std::ranges::find(state->editor.selectedEntities, entries[i].entity) == state->editor.selectedEntities.end()) {
                                    state->editor.selectedEntities.PushBack(entries[i].entity);
                                }
                            }
                        }
                        else {
                            state->editor.selectedEntities.Clear();
                            state->editor.selectedEntities.PushBack(e.entity);
                            s_selectionAnchor = e.entity;
                        }
                    }
                    else if (ctrlHeld) {
                        auto it = std::ranges::find(state->editor.selectedEntities, e.entity);
                        if (it != state->editor.selectedEntities.end()) {
                            state->editor.selectedEntities.Remove(it);
                        }
                        else {
                            state->editor.selectedEntities.PushBack(e.entity);
                        }
                        s_selectionAnchor = e.entity;
                    }
                    else {
                        state->editor.selectedEntities.Clear();
                        state->editor.selectedEntities.PushBack(e.entity);
                        s_selectionAnchor = e.entity;
                    }
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("SCENE_ENTITY", &e.entity, sizeof(e.entity));
                    const bool inSelection = std::ranges::find(state->editor.selectedEntities, e.entity) != state->editor.selectedEntities.end();
                    if (inSelection && state->editor.selectedEntities.Size() > 1) {
                        ImGui::Text("%d entities", static_cast<int>(state->editor.selectedEntities.Size()));
                    }
                    else {
                        ImGui::TextUnformatted(e.label);
                    }
                    ImGui::EndDragDropSource();
                }
                if (!filterActive && ImGui::BeginDragDropTarget()) {
                    const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SCENE_ENTITY", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                    if (p) {
                        const entt::entity dragged = *static_cast<const entt::entity*>(p->Data);
                        // Top/bottom 25% reorders as a sibling of the target. Middle 50% parents onto the target.
                        if (dragged != e.entity && !isAncestorOf(dragged, e.entity)) {
                            const ImVec2 mn = ImGui::GetItemRectMin();
                            const ImVec2 mx = ImGui::GetItemRectMax();
                            const float height = mx.y - mn.y;
                            const float frac = height > 0.0f ? (ImGui::GetMousePos().y - mn.y) / height : 0.5f;
                            if (frac < 0.25f || frac > 0.75f) {
                                const bool below = frac > 0.75f;
                                const float lineY = below ? mx.y : mn.y;
                                ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, lineY), ImVec2(mx.x, lineY), IM_COL32(255, 220, 0, 255), 2.0f);
                                if (p->IsDelivery()) {
                                    reorderDragged = dragged;
                                    reorderTarget = e.entity;
                                    reorderBelow = below;
                                }
                            }
                            else {
                                ImGui::GetWindowDrawList()->AddRect(mn, mx, IM_COL32(0, 200, 255, 255), 0.0f, 0, 2.0f);
                                if (p->IsDelivery()) {
                                    parentDragged = dragged;
                                    parentTarget = e.entity;
                                }
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            if (isPrefab) ImGui::PopStyleColor();
            ImGui::PopID();
            return hasChildren && open;
        };

        // Flat draw (used while filtering)
        auto drawGroup = [&](Core::Span<EntityEntry*> group) {
            for (size_t i = 0; i < group.Size(); ++i) {
                const EntityEntry* prev = i > 0 ? group[i - 1] : nullptr;
                const EntityEntry* next = (i + 1 < group.Size()) ? group[i + 1] : nullptr;
                drawEntityRow(*group[i], prev, next, false);
            }
        };

        // Nested draw
        auto drawSubtree = [&](this auto&& drawSubtree, Core::Span<EntityEntry*> group) -> void {
            for (size_t i = 0; i < group.Size(); ++i) {
                const EntityEntry* prev = i > 0 ? group[i - 1] : nullptr;
                const EntityEntry* next = (i + 1 < group.Size()) ? group[i + 1] : nullptr;
                Core::Span<EntityEntry*> kids = collectChildren(group[i]->entity);
                const bool open = drawEntityRow(*group[i], prev, next, !kids.IsEmpty());
                if (open) {
                    ImGui::Indent();
                    drawSubtree(kids);
                    ImGui::Unindent();
                }
            }
        };

        // Folder anchors
        struct AnchorInfo
        {
            entt::entity entity;
            StringID id;
            StringID parent;
            const char* name;
        };
        Core::ArenaVector<AnchorInfo> anchors{&ctx->editorArena.Get(), 64}; {
            auto av = state->registry.view<Component::SceneFolderComponent, Component::SceneComponent>();
            for (auto a : av) {
                if (av.get<Component::SceneComponent>(a).sceneId != state->scene.currentSceneId) { continue; }
                const auto& fc = av.get<Component::SceneFolderComponent>(a);
                anchors.PushBack({a, fc.folderId, fc.parentFolder, fc.name.c_str()});
            }
        }

        auto anchorExists = [&](StringID folderId) {
            for (auto& a : anchors) { if (a.id == folderId) { return true; } }
            return false;
        };
        auto folderHasMembers = [&](StringID folderId) {
            auto fv = state->registry.view<Component::EntityFolderComponent, Component::SceneComponent>();
            for (auto en : fv) {
                if (fv.get<Component::SceneComponent>(en).sceneId != state->scene.currentSceneId) { continue; }
                if (fv.get<Component::EntityFolderComponent>(en).folderId == folderId) { return true; }
            }
            return false;
        };
        auto folderHasChildren = [&](StringID folderId) {
            for (auto& a : anchors) { if (a.parent == folderId) { return true; } }
            return false;
        };

        const bool expandFoldersForFilter = filterActive && !state->editor.sceneBrowserFilterWasActive;

        auto folderSelected = [&](entt::entity f) {
            return std::ranges::find(state->editor.selectedFolders, f) != state->editor.selectedFolders.end();
        };

        auto folderClicked = [&](entt::entity folderEntity, Core::Span<AnchorInfo*> siblings) {
            const bool ctrlHeld = state->input.GetActionState(Actions::ACTION_MODIFIER_CTRL).down;
            const bool shiftHeld = state->input.GetActionState(Actions::ACTION_MODIFIER_SHIFT).down;
            state->editor.selectedEntities.Clear();

            if (shiftHeld && s_selectionAnchor != entt::null) {
                int anchorIdx = -1;
                int clickedIdx = -1;
                for (int i = 0; i < static_cast<int>(siblings.Size()); ++i) {
                    if (siblings[i]->entity == s_selectionAnchor) { anchorIdx = i; }
                    if (siblings[i]->entity == folderEntity) { clickedIdx = i; }
                }
                if (anchorIdx >= 0 && clickedIdx >= 0) {
                    if (anchorIdx > clickedIdx) { std::swap(anchorIdx, clickedIdx); }
                    if (!ctrlHeld) { state->editor.selectedFolders.Clear(); }
                    for (int i = anchorIdx; i <= clickedIdx; ++i) {
                        if (!folderSelected(siblings[i]->entity)) { state->editor.selectedFolders.PushBack(siblings[i]->entity); }
                    }
                }
                else {
                    state->editor.selectedFolders.Clear();
                    state->editor.selectedFolders.PushBack(folderEntity);
                    s_selectionAnchor = folderEntity;
                }
            }
            else if (ctrlHeld) {
                auto it = std::ranges::find(state->editor.selectedFolders, folderEntity);
                if (it != state->editor.selectedFolders.end()) {
                    state->editor.selectedFolders.Remove(it);
                }
                else {
                    state->editor.selectedFolders.PushBack(folderEntity);
                }
                s_selectionAnchor = folderEntity;
            }
            else {
                state->editor.selectedFolders.Clear();
                state->editor.selectedFolders.PushBack(folderEntity);
                s_selectionAnchor = folderEntity;
            }
        };

        // Folder node
        auto drawFolderNode = [&](const AnchorInfo& a, const char* idPrefix, Core::Span<AnchorInfo*> siblings) -> bool {
            if (state->editor.renamingEntity == a.entity) {
                ImGui::PushID(Core::InlineString<64>::Format("%s%llu", idPrefix, static_cast<unsigned long long>(a.id.id)).c_str());
                if (state->editor.renameRequestFocus) {
                    ImGui::SetKeyboardFocusHere();
                    state->editor.renameRequestFocus = false;
                }
                ImGui::SetNextItemWidth(-1);
                const bool committed = ImGui::InputText("##folder_rename", state->editor.renameBuffer, sizeof(state->editor.renameBuffer),
                                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (committed || ImGui::IsItemDeactivated()) {
                    if (auto* fc = state->registry.try_get<Component::SceneFolderComponent>(a.entity)) {
                        if (strcmp(fc->name.c_str(), state->editor.renameBuffer) != 0) {
                            fc->name = Core::ShortString(state->editor.renameBuffer);
                            MarkSceneModified(state, state->scene.currentSceneId);
                        }
                    }
                    state->editor.renamingEntity = entt::null;
                }
                ImGui::PopID();
                return false;
            }

            if (expandFoldersForFilter) { ImGui::SetNextItemOpen(true, ImGuiCond_Always); }
            const ImGuiTreeNodeFlags folderFlags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                   (folderSelected(a.entity) ? ImGuiTreeNodeFlags_Selected : 0);
            bool open = ImGui::TreeNodeEx(Core::InlineString<192>::Format("%s##%s%llu", a.name, idPrefix, static_cast<unsigned long long>(a.id.id)).c_str(), folderFlags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                folderClicked(a.entity, siblings);
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                ImGui::SetDragDropPayload("SCENE_FOLDER", &a.entity, sizeof(a.entity));
                ImGui::TextUnformatted(a.name);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SCENE_ENTITY")) {
                    moveToFolderEntity = *static_cast<const entt::entity*>(p->Data);
                    moveToFolderId = a.id;
                }
                if (!a.parent.IsValid()) {
                    const ImGuiPayload* fp = ImGui::AcceptDragDropPayload("SCENE_FOLDER", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                    if (fp) {
                        const entt::entity draggedAnchor = *static_cast<const entt::entity*>(fp->Data);
                        const auto* dfc = state->registry.try_get<Component::SceneFolderComponent>(draggedAnchor);
                        const bool valid = dfc && draggedAnchor != a.entity && !folderHasChildren(dfc->folderId);
                        if (valid) {
                            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 220, 0, 255), 0.0f, 0, 2.0f);
                            if (fp->IsDelivery()) {
                                reparentFolderEntity = draggedAnchor;
                                reparentFolderTo = a.id;
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem(Core::InlineString<64>::Format("folderctx##%llu", static_cast<unsigned long long>(a.id.id)).c_str())) {
                static char nameBuf[64];
                if (ImGui::IsWindowAppearing()) { strncpy_s(nameBuf, a.name, sizeof(nameBuf) - 1); }
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::InputText("##foldername", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    state->registry.get<Component::SceneFolderComponent>(a.entity).name = Core::ShortString(nameBuf);
                    MarkSceneModified(state, state->scene.currentSceneId);
                    ImGui::CloseCurrentPopup();
                }
                if (!a.parent.IsValid() && ImGui::MenuItem("New Subfolder")) {
                    newSubfolderParent = a.id;
                }
                const bool empty = !folderHasMembers(a.id) && !folderHasChildren(a.id);
                ImGui::BeginDisabled(!empty);
                if (ImGui::MenuItem("Delete Folder")) { folderToDelete = a.entity; }
                ImGui::EndDisabled();
                if (!empty && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("Folder must be empty to delete"); }
                ImGui::EndPopup();
            }
            return open;
        };

        auto drawMembers = [&](StringID folderId) {
            Core::ArenaVector<EntityEntry*> group{&ctx->editorArena.Get(), entries.Size() + 1};
            for (auto& en : entries) {
                if (en.folderId != folderId) { continue; }
                if (!filterActive && en.parentEntity != entt::null) { continue; } // children are drawn nested under their parent
                group.PushBack(&en);
            }
            if (filterActive) { drawGroup(group); }
            else { drawSubtree(group); }
        };

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("##entity_list", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None);

        // Scene root
        {
            const ImGuiPayload* active = ImGui::GetDragDropPayload();
            const bool dragging = active && (active->IsDataType("SCENE_ENTITY") || active->IsDataType("SCENE_FOLDER"));
            if (dragging) {
                ImGui::Selectable("- Scene Root -");
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SCENE_ENTITY")) {
                        moveToFolderEntity = *static_cast<const entt::entity*>(p->Data);
                        moveToFolderId = StringID();
                    }
                    if (const ImGuiPayload* fp = ImGui::AcceptDragDropPayload("SCENE_FOLDER")) {
                        reparentFolderEntity = *static_cast<const entt::entity*>(fp->Data);
                        reparentFolderTo = StringID();
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            else {
                ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
            }
        }

        // Top-level folders
        Core::ArenaVector<AnchorInfo*> topFolders{&ctx->editorArena.Get(), anchors.Size() + 1};
        for (auto& a : anchors) { if (!a.parent.IsValid()) { topFolders.PushBack(&a); } }
        std::ranges::sort(topFolders, [](const AnchorInfo* x, const AnchorInfo* y) { return strcmp(x->name, y->name) < 0; });

        for (auto* a : topFolders) {
            if (drawFolderNode(*a, "folder_", topFolders)) {
                drawMembers(a->id);
                Core::ArenaVector<AnchorInfo*> children{&ctx->editorArena.Get(), anchors.Size() + 1};
                for (auto& c : anchors) { if (c.parent == a->id) { children.PushBack(&c); } }
                std::ranges::sort(children, [](const AnchorInfo* x, const AnchorInfo* y) { return strcmp(x->name, y->name) < 0; });
                for (auto* c : children) {
                    if (drawFolderNode(*c, "subfolder_", children)) {
                        drawMembers(c->id);
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }

        // Root entities (no folder)
        {
            Core::ArenaVector<EntityEntry*> group{&ctx->editorArena.Get(), entries.Size() + 1};
            for (auto& en : entries) {
                const bool noFolder = !en.folderId.IsValid() || !anchorExists(en.folderId);
                if (!noFolder) { continue; }
                if (!filterActive && en.parentEntity != entt::null) { continue; } // children are drawn nested under their parent
                group.PushBack(&en);
            }
            if (filterActive) { drawGroup(group); }
            else { drawSubtree(group); }
        }

        // Auto-scroll
        if (ImGui::GetDragDropPayload() && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            const float mouseY = ImGui::GetMousePos().y;
            const float top = ImGui::GetWindowPos().y;
            const float bottom = top + ImGui::GetWindowSize().y;
            if (mouseY < top + 20.0f) { ImGui::SetScrollY(ImGui::GetScrollY() - 10.0f); }
            else if (mouseY > bottom - 20.0f) { ImGui::SetScrollY(ImGui::GetScrollY() + 10.0f); }
        }

        ImGui::EndChild();
        state->editor.sceneBrowserFilterWasActive = filterActive;

        // Apply deferred
        if (moveToFolderEntity != entt::null) {
            Core::ArenaVector<entt::entity> toMove{&ctx->editorArena.Get(), state->editor.selectedEntities.Size() + 1};
            if (std::ranges::find(state->editor.selectedEntities, moveToFolderEntity) != state->editor.selectedEntities.end()) {
                for (entt::entity e : state->editor.selectedEntities) { toMove.PushBack(e); }
            }
            else {
                toMove.PushBack(moveToFolderEntity);
            }
            std::ranges::sort(toMove, [&](entt::entity a, entt::entity b) {
                const auto* sa = state->registry.try_get<Component::StableIdComponent>(a);
                const auto* sb = state->registry.try_get<Component::StableIdComponent>(b);
                return (sa ? sa->sortOrder : 0) < (sb ? sb->sortOrder : 0);
            });
            uint64_t order = HighestSortOrderInScene(state->registry, state->scene.currentSceneId);
            for (entt::entity e : toMove) {
                ClearParent(state, e);
                state->registry.get_or_emplace<Component::EntityFolderComponent>(e).folderId = moveToFolderId;
                if (auto* st = state->registry.try_get<Component::StableIdComponent>(e)) {
                    st->sortOrder = ++order;
                }
            }
            MarkSceneModified(state, state->scene.currentSceneId);
        }
        if (reparentFolderEntity != entt::null) {
            if (auto* fc = state->registry.try_get<Component::SceneFolderComponent>(reparentFolderEntity)) {
                if (fc->parentFolder != reparentFolderTo && fc->folderId != reparentFolderTo) {
                    fc->parentFolder = reparentFolderTo;
                    MarkSceneModified(state, state->scene.currentSceneId);
                }
            }
        }
        if (reorderDragged != entt::null && reorderTarget != entt::null && reorderDragged != reorderTarget) {
            // The dragged entities become siblings of the target: reparent to the target's level, then position via sort order.
            entt::entity targetParent = entt::null;
            if (auto* th = state->registry.try_get<Component::HierarchyComponent>(reorderTarget); th && state->registry.valid(th->parent)) {
                targetParent = th->parent;
            }
            StringID targetFolder;
            if (auto* tf = state->registry.try_get<Component::EntityFolderComponent>(reorderTarget)) {
                targetFolder = tf->folderId;
            }

            Core::ArenaVector<entt::entity> moved{&ctx->editorArena.Get(), state->editor.selectedEntities.Size() + 1};
            if (std::ranges::find(state->editor.selectedEntities, reorderDragged) != state->editor.selectedEntities.end()) {
                for (entt::entity e : state->editor.selectedEntities) {
                    if (e == reorderTarget) { continue; }
                    moved.PushBack(e);
                }
            }
            else {
                moved.PushBack(reorderDragged);
            }

            for (entt::entity m : moved) {
                if (targetParent == entt::null) {
                    ClearParent(state, m); // sibling of a root
                    state->registry.get_or_emplace<Component::EntityFolderComponent>(m).folderId = targetFolder;
                }
                else {
                    SetParent(state, m, targetParent); // sibling of a child
                }
            }

            Core::ArenaVector<entt::entity> order{&ctx->editorArena.Get(), totalInScene + 1};
            auto rv = state->registry.view<Component::SceneComponent, Component::StableIdComponent>();
            for (auto en : rv) {
                if (rv.get<Component::SceneComponent>(en).sceneId == state->scene.currentSceneId) { order.PushBack(en); }
            }
            std::ranges::sort(order, [&](entt::entity a, entt::entity b) {
                return state->registry.get<Component::StableIdComponent>(a).sortOrder < state->registry.get<Component::StableIdComponent>(b).sortOrder;
            });
            std::ranges::sort(moved, [&](entt::entity a, entt::entity b) {
                return state->registry.get<Component::StableIdComponent>(a).sortOrder < state->registry.get<Component::StableIdComponent>(b).sortOrder;
            });
            auto inMoved = [&](entt::entity e) {
                for (entt::entity m : moved) { if (m == e) { return true; } }
                return false;
            };

            Core::ArenaVector<entt::entity> finalOrder{&ctx->editorArena.Get(), order.Size() + 1};
            for (entt::entity en : order) {
                if (inMoved(en)) { continue; }
                if (en == reorderTarget && !reorderBelow) { for (entt::entity m : moved) { finalOrder.PushBack(m); } }
                finalOrder.PushBack(en);
                if (en == reorderTarget && reorderBelow) { for (entt::entity m : moved) { finalOrder.PushBack(m); } }
            }
            uint64_t n = 1;
            for (entt::entity en : finalOrder) { state->registry.get<Component::StableIdComponent>(en).sortOrder = n++; }
            MarkSceneModified(state, state->scene.currentSceneId);
        }
        if (parentDragged != entt::null && parentTarget != entt::null) {
            Core::ArenaVector<entt::entity> moved{&ctx->editorArena.Get(), state->editor.selectedEntities.Size() + 1};
            if (std::ranges::find(state->editor.selectedEntities, parentDragged) != state->editor.selectedEntities.end()) {
                for (entt::entity e : state->editor.selectedEntities) {
                    if (e == parentTarget) { continue; }
                    moved.PushBack(e);
                }
            }
            else {
                moved.PushBack(parentDragged);
            }
            for (entt::entity m : moved) {
                SetParent(state, m, parentTarget); // keeps world pose, rejects cycles
            }
            MarkSceneModified(state, state->scene.currentSceneId);
        }
        if (newSubfolderParent.IsValid()) {
            entt::entity f = state->registry.create();
            state->registry.emplace<Component::SceneComponent>(f, state->scene.currentSceneId);
            Component::SceneFolderComponent folder{};
            folder.folderId = StringID{state->rng()};
            folder.parentFolder = newSubfolderParent;
            folder.name = Core::ShortString("New Subfolder");
            state->registry.emplace<Component::SceneFolderComponent>(f, std::move(folder));
            MarkSceneModified(state, state->scene.currentSceneId);
        }
        if (folderToDelete != entt::null) {
            state->registry.destroy(folderToDelete);
            MarkSceneModified(state, state->scene.currentSceneId);
        }

        ImGui::Separator();
        if (ImGui::SmallButton("Compact Order")) {
            Core::ArenaVector<entt::entity> ordered{&ctx->editorArena.Get(), totalInScene + 1};
            auto compactView = state->registry.view<Component::SceneComponent, Component::StableIdComponent>();
            for (auto entity : compactView) {
                if (compactView.get<Component::SceneComponent>(entity).sceneId == state->scene.currentSceneId) { ordered.PushBack(entity); }
            }
            std::ranges::sort(ordered, [&](entt::entity a, entt::entity b) {
                return state->registry.get<Component::StableIdComponent>(a).sortOrder < state->registry.get<Component::StableIdComponent>(b).sortOrder;
            });
            uint64_t next = 1;
            for (entt::entity e : ordered) {
                state->registry.get<Component::StableIdComponent>(e).sortOrder = next++;
            }
            if (next > 1) { MarkSceneModified(state, state->scene.currentSceneId); }
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Reassign gap-free 1..N sort order to all entities in this scene, preserving current order"); }
        ImGui::SameLine();
        if (filterActive) {
            ImGui::Text("%d entities (%d shown)", static_cast<int>(totalInScene), static_cast<int>(entries.Size()));
        }
        else {
            ImGui::Text("%d entities", static_cast<int>(totalInScene));
        }
        if (bEntriesTruncated) {
            ImGui::SameLine();
            ImGui::TextColored({1.0f, 0.75f, 0.2f, 1.0f}, "(capped at %d - search to narrow)", static_cast<int>(MAX_BROWSER_ENTRIES));
        }

        const size_t selCount = state->editor.selectedEntities.Size();
        if (totalInScene > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (selCount > 0) {
                ImGui::Text("%zu selected", selCount);
                ImGui::SameLine();
            }
            if (ImGui::SmallButton("Sort...")) { ImGui::OpenPopup("sort_selection"); }
            if (selCount == 0 && ImGui::IsItemHovered()) { ImGui::SetTooltip("Sort all entities in this scene"); }
            if (ImGui::BeginPopup("sort_selection")) {
                Core::ArenaVector<entt::entity> target{&ctx->editorArena.Get(), totalInScene + 1};
                if (selCount > 0) {
                    for (entt::entity e : state->editor.selectedEntities) { target.PushBack(e); }
                }
                else {
                    auto sv = state->registry.view<Component::SceneComponent, Component::StableIdComponent>();
                    for (auto e : sv) {
                        if (sv.get<Component::SceneComponent>(e).sceneId == state->scene.currentSceneId) { target.PushBack(e); }
                    }
                }

                auto nameOf = [&](entt::entity e) -> const char* {
                    const auto* nc = state->registry.try_get<Component::NameComponent>(e);
                    return nc ? nc->name.c_str() : "Unnamed";
                };
                auto reassignSlots = [&]() {
                    Core::ArenaVector<uint64_t> slots{&ctx->editorArena.Get(), target.Size() + 1};
                    for (entt::entity e : target) {
                        const auto* s = state->registry.try_get<Component::StableIdComponent>(e);
                        slots.PushBack(s ? s->sortOrder : 0);
                    }
                    std::ranges::sort(slots);
                    for (size_t i = 0; i < target.Size(); ++i) {
                        if (auto* s = state->registry.try_get<Component::StableIdComponent>(target[i])) { s->sortOrder = slots[i]; }
                    }
                    if (selCount > 0) {
                        state->editor.selectedEntities.Clear();
                        for (entt::entity e : target) { state->editor.selectedEntities.PushBack(e); }
                    }
                    MarkSceneModified(state, state->scene.currentSceneId);
                };
                if (ImGui::MenuItem("Name (A-Z)")) {
                    std::ranges::sort(target, [&](entt::entity a, entt::entity b) { return strcmp(nameOf(a), nameOf(b)) < 0; });
                    reassignSlots();
                }
                if (ImGui::MenuItem("Name (Z-A)")) {
                    std::ranges::sort(target, [&](entt::entity a, entt::entity b) { return strcmp(nameOf(a), nameOf(b)) > 0; });
                    reassignSlots();
                }
                if (ImGui::MenuItem("Reverse")) {
                    std::ranges::reverse(target);
                    reassignSlots();
                }
                if (ImGui::MenuItem("Random")) {
                    std::ranges::shuffle(target, state->rng);
                    reassignSlots();
                }
                ImGui::EndPopup();
            }
        }
    }
    ImGui::End();
}
}
