//
// Created by William on 2026-06-26.
//

#include "editor_scene_panels.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"
#include "engine/editor/editor_systems.h"
#include "engine/systems/scene_system.h"
#include "engine/input/engine_actions.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/asset_manager.h"
#include "engine/input/input_frame.h"
#include "core/containers/arena_array.h"
#include "core/containers/arena_fixed_vector.h"
#include "engine/components/fwd_components.h"
#include "engine/components/common_components.h"
#include "engine/components/editor_components.h"
#include "engine/components/scene_components.h"

namespace Engine
{
void DrawOutliner(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->editor.renamingEntity != entt::null && !state->registry.valid(state->editor.renamingEntity)) {
        state->editor.renamingEntity = entt::null;
    }
    for (int i = static_cast<int>(state->editor.selectedFolders.Size()) - 1; i >= 0; --i) {
        if (!state->registry.valid(state->editor.selectedFolders[i])) {
            state->editor.selectedFolders.Remove(state->editor.selectedFolders.begin() + i);
        }
    }

    bool& bOpen = state->editor.windowOpen[EDITOR_WINDOW_OUTLINER];
    if (!bOpen) { return; }
    if (ImGui::Begin(EDITOR_WINDOWS[EDITOR_WINDOW_OUTLINER].title, &bOpen)) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##active_scene", state->scene.currentSceneId.IsValid() ? state->scene.currentSceneName.c_str() : "No scene loaded")) {
            for (const RuntimeSceneMetadata& loaded : state->editor.loadedScenes) {
                const auto* meta = ctx->assetManager->GetSceneMetadata(loaded.sceneId);
                const bool bModified = state->editor.modifiedScenes.Contains(loaded.sceneId);
                const Core::InlineString<160> label = Core::InlineString<160>::Format("%s%s##%llu", meta ? meta->sceneName.c_str() : "(unregistered)", bModified ? " *" : "",
                                                                                      static_cast<unsigned long long>(loaded.sceneId.id));
                if (ImGui::Selectable(label.c_str(), loaded.sceneId == state->scene.currentSceneId)) {
                    state->scene.currentSceneId = loaded.sceneId;
                    SyncActiveScene(ctx, state);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Active scene: the outliner shows it, and new entities go into it"); }

        ImGui::BeginDisabled(!state->scene.currentSceneId.IsValid());
        const bool bNewFolder = ImGui::Button("New Folder");
        ImGui::EndDisabled();
        if (bNewFolder) {
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

        constexpr size_t MAX_BROWSER_ENTRIES = 16384;

        auto view2 = state->registry.view<Component::SceneComponent>();
        Core::ArenaVector<EntityEntry> entries{&ctx->editorArena.Get(), MAX_BROWSER_ENTRIES + 1};

        size_t totalInScene = 0;
        bool bEntriesTruncated = false;
        for (int pass = 0; pass < 2; ++pass) {
            for (auto entity : view2) {
                auto& scene = view2.get<Component::SceneComponent>(entity);
                if (scene.sceneId != state->scene.currentSceneId) continue;
                if (state->registry.all_of<Component::SceneFolderComponent>(entity)) continue;
                if (pass == 0) { ++totalInScene; }

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
                if ((parentEntity == entt::null) != (pass == 0)) { continue; }
                if (entries.Size() >= MAX_BROWSER_ENTRIES) {
                    bEntriesTruncated = true;
                    continue;
                }
                entries.PushBack({entity, label, stableId, sortOrder, folderId, parentEntity, depth});
            }
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
        enum class EntityMenuAction { None, Group, Rename, Copy, Paste, Duplicate, Delete };
        EntityMenuAction entityAction = EntityMenuAction::None;
        // Applied next frame to the picked folder, or with no folder to the selection.
        static int32_t pendingExpandAll = -1;
        static StringID pendingExpandFolder{};
        const int32_t expandAll = pendingExpandAll;
        const StringID expandFolder = pendingExpandFolder;
        pendingExpandAll = -1;
        auto entityOpenId = [](entt::entity e) { return ImHashData(&e, sizeof(e), ImHashStr("hierarchy_open")); };
        auto drawExpandAllItems = [&](StringID folder) {
            const bool bExpand = ImGui::MenuItem("Expand All");
            const bool bCollapse = ImGui::MenuItem("Collapse All Children");
            if (bExpand || bCollapse) {
                pendingExpandAll = bExpand ? 1 : 0;
                pendingExpandFolder = folder;
            }
        };
        static entt::entity lastRevealed = entt::null;
        const entt::entity newestSelected = state->editor.selectedEntities.IsEmpty() ? entt::null : state->editor.selectedEntities[state->editor.selectedEntities.Size() - 1];
        const entt::entity revealEntity = newestSelected != lastRevealed ? newestSelected : entt::null;
        lastRevealed = newestSelected;
        StringID revealFolder{};
        StringID revealFolderParent{};
        Core::ArenaVector<entt::entity> visibleRows{&ctx->editorArena.Get(), entries.Size() + 1};
        entt::entity rangeTarget = entt::null;

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

        auto topLevelOf = [&](entt::entity dragged, entt::entity exclude) {
            Core::ArenaVector<entt::entity> out{&ctx->editorArena.Get(), state->editor.selectedEntities.Size() + 1};
            auto& selection = state->editor.selectedEntities;
            if (std::ranges::find(selection, dragged) == selection.end()) {
                out.PushBack(dragged);
                return out;
            }
            for (entt::entity e : selection) {
                if (e == exclude || !state->registry.valid(e)) { continue; }
                bool bNested = false;
                for (entt::entity other : selection) { bNested |= other != e && isAncestorOf(other, e); }
                if (!bNested) { out.PushBack(e); }
            }
            return out;
        };

        // Entity row. Returns true if the row is an expanded parent (caller should recurse into children).
        auto drawEntityRow = [&](const EntityEntry& e, const EntityEntry* prev, const EntityEntry* next, bool hasChildren) -> bool {
            visibleRows.PushBack(e.entity);
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
            const ImGuiID openId = entityOpenId(e.entity);
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

            bool bRowSelectable = false;
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
                        rangeTarget = e.entity;
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
                if (e.entity == revealEntity && !ImGui::IsItemVisible()) { ImGui::SetScrollHereY(0.5f); }
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
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) {
                    state->editor.selectedFolders.Clear();
                    state->editor.selectedEntities.Clear();
                    state->editor.selectedEntities.PushBack(e.entity);
                    s_selectionAnchor = e.entity;
                }
                bRowSelectable = true;
            }
            if (isPrefab) ImGui::PopStyleColor();
            if (bRowSelectable && ImGui::BeginPopupContextItem("entity_ctx")) {
                const size_t count = state->editor.selectedEntities.Size();
                if (ImGui::MenuItem(count > 1 ? "Group Under New Entity" : "Parent Under New Entity")) { entityAction = EntityMenuAction::Group; }
                ImGui::Separator();
                if (ImGui::MenuItem("Rename", "F2", false, count == 1)) { entityAction = EntityMenuAction::Rename; }
                if (ImGui::MenuItem("Copy", "Ctrl+C")) { entityAction = EntityMenuAction::Copy; }
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, CanPasteEntities(ctx))) { entityAction = EntityMenuAction::Paste; }
                if (ImGui::MenuItem("Duplicate", "Ctrl+W")) { entityAction = EntityMenuAction::Duplicate; }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del")) { entityAction = EntityMenuAction::Delete; }
                ImGui::Separator();
                drawExpandAllItems(StringID());
                ImGui::EndPopup();
            }
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
            else if (expandAll >= 0 && expandFolder.IsValid() && ((a.id == expandFolder && expandAll != 0) || a.parent == expandFolder)) {
                ImGui::SetNextItemOpen(expandAll != 0, ImGuiCond_Always);
            }
            else if (revealFolder.IsValid() && (a.id == revealFolder || a.id == revealFolderParent)) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
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
                ImGui::Separator();
                drawExpandAllItems(a.id);
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
        if (expandAll >= 0) {
            ImGuiStorage* storage = ImGui::GetStateStorage();
            auto inScope = [&](const EntityEntry& en) {
                if (expandFolder.IsValid()) {
                    if (en.folderId == expandFolder) { return true; }
                    for (const AnchorInfo& a : anchors) {
                        if (a.id == en.folderId && a.parent == expandFolder) { return true; }
                    }
                    return false;
                }
                for (const entt::entity selected : state->editor.selectedEntities) {
                    if ((selected == en.entity && expandAll != 0) || isAncestorOf(selected, en.entity)) { return true; }
                }
                return false;
            };
            for (const auto& en : entries) {
                if (inScope(en)) { storage->SetInt(entityOpenId(en.entity), expandAll); }
            }
        }
        if (revealEntity != entt::null && state->registry.valid(revealEntity)) {
            ImGuiStorage* storage = ImGui::GetStateStorage();
            entt::entity root = revealEntity;
            for (int guard = 0; guard < 1024; ++guard) {
                const auto* h = state->registry.try_get<Component::HierarchyComponent>(root);
                if (!h || !state->registry.valid(h->parent)) { break; }
                root = h->parent;
                storage->SetInt(entityOpenId(root), 1);
            }
            for (const auto& en : entries) {
                if (en.entity == root) { revealFolder = en.folderId; }
            }
            for (const AnchorInfo& a : anchors) {
                if (a.id == revealFolder) { revealFolderParent = a.parent; }
            }
        }

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

        if (rangeTarget != entt::null) {
            const auto anchorIt = std::ranges::find(visibleRows, s_selectionAnchor);
            const auto targetIt = std::ranges::find(visibleRows, rangeTarget);
            if (!state->input.GetActionState(Actions::ACTION_MODIFIER_CTRL).down) { state->editor.selectedEntities.Clear(); }
            if (anchorIt == visibleRows.end()) {
                state->editor.selectedEntities.PushBack(rangeTarget);
                s_selectionAnchor = rangeTarget;
            }
            else {
                auto first = anchorIt;
                auto last = targetIt;
                if (first > last) { std::swap(first, last); }
                for (auto it = first; it <= last; ++it) {
                    if (std::ranges::find(state->editor.selectedEntities, *it) == state->editor.selectedEntities.end()) {
                        state->editor.selectedEntities.PushBack(*it);
                    }
                }
            }
        }

        // Apply deferred
        if (moveToFolderEntity != entt::null) {
            Core::ArenaVector<entt::entity> toMove = topLevelOf(moveToFolderEntity, entt::null);
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

            Core::ArenaVector<entt::entity> moved = topLevelOf(reorderDragged, reorderTarget);

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
            Core::ArenaVector<entt::entity> moved = topLevelOf(parentDragged, parentTarget);
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
        switch (entityAction) {
            case EntityMenuAction::Group:
                GroupEntities(ctx, state, state->editor.selectedEntities);
                break;
            case EntityMenuAction::Rename: {
                const entt::entity target = state->editor.selectedEntities[0];
                const auto* nc = state->registry.try_get<Component::NameComponent>(target);
                state->editor.renamingEntity = target;
                state->editor.renameRequestFocus = true;
                strncpy_s(state->editor.renameBuffer, nc ? nc->name.c_str() : "", sizeof(state->editor.renameBuffer) - 1);
                break;
            }
            case EntityMenuAction::Copy:
                CopyEntitiesToClipboard(ctx, state, state->editor.selectedEntities);
                break;
            case EntityMenuAction::Paste:
                PasteEntitiesFromClipboard(ctx, state);
                break;
            case EntityMenuAction::Duplicate:
                DuplicateEntities(ctx, state, state->editor.selectedEntities);
                break;
            case EntityMenuAction::Delete:
                DeleteSelectedEntities(ctx, state);
                break;
            case EntityMenuAction::None:
                break;
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
