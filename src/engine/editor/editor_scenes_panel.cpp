//
// Created by William on 2026-09-29.
//

#include "editor_scene_panels.h"

#include <algorithm>
#include <cstring>

#include "imgui.h"
#include "engine/editor/editor_systems.h"
#include "engine/editor/probe_bake_system.h"
#include "engine/systems/scene_system.h"
#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "engine/asset_manager.h"
#include "engine/logging/engine_log.h"
#include "engine/project_config.h"
#include "core/containers/arena_fixed_vector.h"
#include "platform/file_utils.h"
#include "platform/paths.h"

namespace Engine
{
enum class ScenesPopup : uint8_t { None, NewScene, NewFolder, RenameScene, RenameFolder, DeleteScene, ConfirmUnload };

struct ScenesPanelState
{
    char search[64]{};
    ScenesPopup request{ScenesPopup::None};
    StringID scene{};
    Core::Path folder{};
    char name[128]{};
    Core::InlineVector<StringID, MAX_LOADED_SCENES> unloadIds{};
};

static ScenesPanelState gScenesPanel{};

static const char* PopupTitle(ScenesPopup popup)
{
    switch (popup) {
        case ScenesPopup::NewScene: return "New Scene";
        case ScenesPopup::NewFolder: return "New Folder";
        case ScenesPopup::RenameScene: return "Rename Scene";
        case ScenesPopup::RenameFolder: return "Rename Folder";
        case ScenesPopup::DeleteScene: return "Delete Scene";
        case ScenesPopup::ConfirmUnload: return "Unsaved Changes";
        default: return "";
    }
}

static int SnakeCaseFilter(ImGuiInputTextCallbackData* data)
{
    const ImWchar c = data->EventChar;
    if (c >= 'A' && c <= 'Z') { data->EventChar = static_cast<ImWchar>(c - 'A' + 'a'); }
    else if (c == ' ' || c == '-') { data->EventChar = '_'; }
    const ImWchar e = data->EventChar;
    return (e >= 'a' && e <= 'z') || (e >= '0' && e <= '9') || e == '_' ? 0 : 1;
}

static bool IsLoaded(const EngineState* state, StringID sceneId)
{
    for (const RuntimeSceneMetadata& scene : state->editor.loadedScenes) {
        if (scene.sceneId == sceneId) { return true; }
    }
    return false;
}

static bool IsNameInUse(EngineContext* ctx, const char* name)
{
    for (const auto& [id, meta] : ctx->assetManager->GetSceneCache()) {
        if (meta.sceneName == name) { return true; }
    }
    return false;
}

static std::string_view RelativeToRoot(const Core::Path& path)
{
    const Core::Path& root = Platform::GetScenePath();
    const std::string_view view = path.View();
    return view.size() > root.Size() ? view.substr(root.Size() + 1) : std::string_view{};
}

static void FolderText(const Core::Path& folder)
{
    const std::string_view relative = RelativeToRoot(folder);
    ImGui::Text("In scenes/%.*s", static_cast<int>(relative.size()), relative.data());
}

static bool IsInsideFolder(const Core::Path& path, const Core::Path& folder)
{
    const std::string_view view = path.View();
    return view.size() > folder.Size() && view.starts_with(folder.View()) && view[folder.Size()] == '/';
}

static bool IsFolderEmpty(EngineContext* ctx, const Core::Path& folder)
{
    for (const auto& [id, meta] : ctx->assetManager->GetSceneCache()) {
        if (IsInsideFolder(meta.source, folder)) { return false; }
    }
    for (const auto& [id, run] : ctx->assetManager->GetPlayCache()) {
        if (IsInsideFolder(run.source, folder)) { return false; }
    }
    for (const Core::Path& other : ctx->assetManager->GetSceneFolders()) {
        if (IsInsideFolder(other, folder)) { return false; }
    }
    return true;
}

static void Request(ScenesPopup popup, StringID scene, const Core::Path& folder, std::string_view name)
{
    gScenesPanel.request = popup;
    gScenesPanel.scene = scene;
    gScenesPanel.folder = folder;
    const size_t len = std::min(name.size(), sizeof(gScenesPanel.name) - 1);
    memcpy(gScenesPanel.name, name.data(), len);
    gScenesPanel.name[len] = '\0';
}

static void LoadAndActivate(EngineContext* ctx, EngineState* state, StringID sceneId)
{
    if (LoadSceneFromFile(state, ctx->assetManager, sceneId).bSuccess) {
        state->scene.currentSceneId = sceneId;
        SyncActiveScene(ctx, state);
    }
}

static void RequestUnload(EngineState* state, Core::Span<const StringID> sceneIds)
{
    gScenesPanel.unloadIds.Clear();
    bool bAnyModified = false;
    for (StringID id : sceneIds) {
        gScenesPanel.unloadIds.PushBack(id);
        bAnyModified |= state->editor.modifiedScenes.Contains(id);
    }
    if (bAnyModified) {
        gScenesPanel.request = ScenesPopup::ConfirmUnload;
        return;
    }
    UnloadScenes(state, gScenesPanel.unloadIds);
    gScenesPanel.unloadIds.Clear();
}

void SyncActiveScene(EngineContext* ctx, EngineState* state)
{
    if (!IsLoaded(state, state->scene.currentSceneId)) {
        state->scene.currentSceneId = state->editor.loadedScenes.IsEmpty() ? StringID{} : state->editor.loadedScenes[0].sceneId;
    }
    const auto* meta = ctx->assetManager->GetSceneMetadata(state->scene.currentSceneId);
    if (meta) {
        state->scene.currentSceneName = meta->sceneName;
    }
    else if (!state->scene.currentSceneId.IsValid()) {
        state->scene.currentSceneName.Clear();
    }
}

static void DrawRunsMenu(EngineContext* ctx, EngineState* state, StringID sceneId)
{
    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
    ImGui::MenuItem("Skip captures", nullptr, &state->playtest.bSkipCaptures);
    ImGui::PopItemFlag();
    ImGui::Separator();
    const bool bRunBusy = state->playtest.bActive || !state->playtest.pendingPath.IsEmpty() || ProbeBakeActive(state);
    uint32_t runCount = 0;
    for (const auto& [id, run] : ctx->assetManager->GetPlayCache()) {
        if (run.sceneId != sceneId) { continue; }
        ++runCount;
        ImGui::PushID(static_cast<int>(id.id));
        if (ImGui::MenuItem(run.name.c_str(), Core::InlineString<32>::Format("%u events", run.eventCount).c_str(), false, !bRunBusy)) {
            state->playtest.Arm(run.source.c_str());
        }
        ImGui::PopID();
    }
    if (runCount == 0) {
        ImGui::TextDisabled("No .wplay for this scene");
    }
}

static void DrawSceneRow(EngineContext* ctx, EngineState* state, StringID sceneId, const AssetManager::CachedSceneMetadata& meta, bool bShowFolder)
{
    const bool bLoaded = IsLoaded(state, sceneId);
    const bool bActive = sceneId == state->scene.currentSceneId;
    const bool bModified = state->editor.modifiedScenes.Contains(sceneId);
    const bool bCanSave = ctx->bGameLoaded && !IsPlaying(state);

    if (!bLoaded) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); }
    const Core::InlineString<192> label = Core::InlineString<192>::Format("%s%s##scene%llu", meta.sceneName.c_str(), bModified ? " *" : "", static_cast<unsigned long long>(sceneId.id));
    if (ImGui::Selectable(label.c_str(), bActive, ImGuiSelectableFlags_AllowDoubleClick)) {
        if (bLoaded) {
            state->scene.currentSceneId = sceneId;
            SyncActiveScene(ctx, state);
        }
        else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && state->editor.loadedScenes.Size() < MAX_LOADED_SCENES) {
            LoadAndActivate(ctx, state, sceneId);
        }
    }
    if (!bLoaded) { ImGui::PopStyleColor(); }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\nid %llu\n%s", meta.source.c_str(), static_cast<unsigned long long>(sceneId.id), bLoaded ? "Click to make active" : "Double-click to load");
    }
    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("SCENE_ASSET", &sceneId, sizeof(sceneId));
        ImGui::TextUnformatted(meta.sceneName.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (!bLoaded) {
            if (ImGui::MenuItem("Load", nullptr, false, state->editor.loadedScenes.Size() < MAX_LOADED_SCENES)) {
                LoadAndActivate(ctx, state, sceneId);
            }
        }
        else {
            if (ImGui::MenuItem("Make Active", nullptr, false, !bActive)) {
                state->scene.currentSceneId = sceneId;
                SyncActiveScene(ctx, state);
            }
            if (ImGui::MenuItem("Save", nullptr, false, bCanSave)) {
                SaveEditorScene(ctx, state, sceneId);
            }
            if (ImGui::MenuItem("Unload")) {
                RequestUnload(state, Core::Span<const StringID>(&sceneId, 1));
            }
        }
        if (ImGui::BeginMenu("Runs", bActive)) {
            DrawRunsMenu(ctx, state, sceneId);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename...")) {
            Request(ScenesPopup::RenameScene, sceneId, meta.source.Parent(), meta.sceneName.View());
        }
        if (ImGui::MenuItem("Set as Default Scene", nullptr, state->projectConfig.defaultScene == sceneId)) {
            state->projectConfig.defaultScene = sceneId;
            WriteProjectConfig(state->projectConfig, state->allocator);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete...", nullptr, false, !bLoaded)) {
            Request(ScenesPopup::DeleteScene, sceneId, meta.source.Parent(), meta.sceneName.View());
        }
        if (bLoaded && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Unload the scene before deleting it");
        }
        ImGui::EndPopup();
    }

    if (meta.bUnsaved) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "no file");
    }
    if (state->projectConfig.defaultScene == sceneId) {
        ImGui::SameLine();
        ImGui::TextDisabled("default");
    }
    if (bShowFolder) {
        const std::string_view folder = RelativeToRoot(meta.source.Parent());
        if (!folder.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.*s", static_cast<int>(folder.size()), folder.data());
        }
    }
}

static void AcceptSceneDrop(EngineContext* ctx, const Core::Path& folder)
{
    if (!ImGui::BeginDragDropTarget()) { return; }
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ASSET")) {
        const StringID sceneId = *static_cast<const StringID*>(payload->Data);
        if (const auto* meta = ctx->assetManager->GetSceneMetadata(sceneId); meta && meta->source.Parent() != folder) {
            ctx->assetManager->MoveScene(sceneId, folder / meta->source.Filename());
        }
    }
    ImGui::EndDragDropTarget();
}

struct SceneEntry
{
    StringID id;
    const AssetManager::CachedSceneMetadata* meta;
};

static void DrawFolder(EngineContext* ctx, EngineState* state, const Core::Path& folder, bool bRoot)
{
    const std::string_view name = bRoot ? std::string_view{"scenes"} : folder.Filename();
    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanFullWidth | (bRoot ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    const bool bOpen = ImGui::TreeNodeEx(Core::InlineString<320>::Format("%.*s##%s", static_cast<int>(name.size()), name.data(), folder.c_str()).c_str(), flags);
    AcceptSceneDrop(ctx, folder);

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("New Scene...")) { Request(ScenesPopup::NewScene, {}, folder, "new_scene"); }
        if (ImGui::MenuItem("New Folder...")) { Request(ScenesPopup::NewFolder, {}, folder, "new_folder"); }
        if (!bRoot) {
            ImGui::Separator();
            if (ImGui::MenuItem("Rename...")) { Request(ScenesPopup::RenameFolder, {}, folder, name); }
            const bool bEmpty = IsFolderEmpty(ctx, folder);
            if (ImGui::MenuItem("Delete", nullptr, false, bEmpty)) {
                if (!Platform::RemoveEmptyDirectory(folder)) {
                    LOG_WARN(Engine, "Folder '{}' still holds files; not deleted", folder.c_str());
                }
                ctx->rescan.bScenes = true;
            }
            if (!bEmpty && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("Only empty folders can be deleted"); }
        }
        ImGui::EndPopup();
    }
    if (!bOpen) { return; }

    Core::ArenaFixedVector<const Core::Path*> subfolders(&ctx->editorArena.Get(), ctx->assetManager->GetSceneFolders().Size() + 1);
    for (const Core::Path& other : ctx->assetManager->GetSceneFolders()) {
        if (other.Parent() == folder) { subfolders.PushBack(&other); }
    }
    std::ranges::sort(subfolders, [](const Core::Path* a, const Core::Path* b) { return a->Filename() < b->Filename(); });
    for (const Core::Path* subfolder : subfolders) {
        DrawFolder(ctx, state, *subfolder, false);
    }

    const auto& sceneCache = ctx->assetManager->GetSceneCache();
    Core::ArenaFixedVector<SceneEntry> scenes(&ctx->editorArena.Get(), sceneCache.Size() + 1);
    for (const auto& [id, meta] : sceneCache) {
        if (meta.source.Parent() == folder) { scenes.PushBack({id, &meta}); }
    }
    std::ranges::sort(scenes, [](const SceneEntry& a, const SceneEntry& b) { return a.meta->sceneName.View() < b.meta->sceneName.View(); });
    for (const SceneEntry& scene : scenes) {
        DrawSceneRow(ctx, state, scene.id, *scene.meta, false);
    }
    ImGui::TreePop();
}

static void DrawSearchResults(EngineContext* ctx, EngineState* state)
{
    const auto& sceneCache = ctx->assetManager->GetSceneCache();
    Core::ArenaFixedVector<SceneEntry> scenes(&ctx->editorArena.Get(), sceneCache.Size() + 1);
    for (const auto& [id, meta] : sceneCache) {
        if (meta.sceneName.Contains(gScenesPanel.search, Core::CaseSensitivity::Insensitive)) { scenes.PushBack({id, &meta}); }
    }
    std::ranges::sort(scenes, [](const SceneEntry& a, const SceneEntry& b) { return a.meta->sceneName.View() < b.meta->sceneName.View(); });
    for (const SceneEntry& scene : scenes) {
        DrawSceneRow(ctx, state, scene.id, *scene.meta, true);
    }
    if (scenes.IsEmpty()) {
        ImGui::TextDisabled("No scene matches");
    }
}

static bool NameInput(bool bAppearing)
{
    if (bAppearing) { ImGui::SetKeyboardFocusHere(); }
    ImGui::SetNextItemWidth(260.0f);
    return ImGui::InputText("##name", gScenesPanel.name, sizeof(gScenesPanel.name),
                            ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll, SnakeCaseFilter);
}

static bool ConfirmButtons(const char* confirmLabel, bool bValid, bool bEnter)
{
    ImGui::BeginDisabled(!bValid);
    const bool bConfirm = ImGui::Button(confirmLabel) || (bEnter && bValid);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
    }
    if (bConfirm) {
        ImGui::CloseCurrentPopup();
    }
    return bConfirm;
}

static void DrawPopups(EngineContext* ctx, EngineState* state)
{
    if (gScenesPanel.request != ScenesPopup::None) {
        ImGui::OpenPopup(PopupTitle(gScenesPanel.request));
        gScenesPanel.request = ScenesPopup::None;
    }

    const char* name = gScenesPanel.name;
    const bool bNameEmpty = name[0] == '\0';

    if (ImGui::BeginPopupModal(PopupTitle(ScenesPopup::NewScene), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        FolderText(gScenesPanel.folder);
        const bool bEnter = NameInput(ImGui::IsWindowAppearing());
        const bool bInUse = !bNameEmpty && IsNameInUse(ctx, name);
        const bool bFull = state->editor.loadedScenes.Size() >= MAX_LOADED_SCENES;
        if (bInUse) { ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "A scene with this name already exists"); }
        if (bFull) { ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%u scenes are loaded; unload one first", MAX_LOADED_SCENES); }
        if (ConfirmButtons("Create", !bNameEmpty && !bInUse && !bFull, bEnter)) {
            const StringID newId{state->rng()};
            ctx->assetManager->RegisterScene(newId, gScenesPanel.folder / Core::InlineString<160>::Format("%s.wscene", name).c_str());
            state->editor.loadedScenes.PushBack({newId});
            state->editor.modifiedScenes.PushBack(newId);
            state->scene.currentSceneId = newId;
            SyncActiveScene(ctx, state);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(PopupTitle(ScenesPopup::NewFolder), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        FolderText(gScenesPanel.folder);
        const bool bEnter = NameInput(ImGui::IsWindowAppearing());
        const Core::Path target = gScenesPanel.folder / name;
        const bool bExists = !bNameEmpty && Platform::FileExists(target);
        if (bExists) { ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Already exists"); }
        if (ConfirmButtons("Create", !bNameEmpty && !bExists, bEnter)) {
            Platform::CreateDirectories(Core::InlineString<264>::Format("%s/", target.c_str()).c_str());
            ctx->rescan.bScenes = true;
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(PopupTitle(ScenesPopup::RenameScene), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto* meta = ctx->assetManager->GetSceneMetadata(gScenesPanel.scene);
        const bool bEnter = NameInput(ImGui::IsWindowAppearing());
        const bool bSame = meta && meta->sceneName == name;
        const bool bInUse = !bNameEmpty && !bSame && IsNameInUse(ctx, name);
        if (bInUse) { ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "A scene with this name already exists"); }
        if (ConfirmButtons("Rename", meta && !bNameEmpty && !bSame && !bInUse, bEnter)) {
            const Core::Path target = meta->source.Parent() / Core::InlineString<160>::Format("%s.wscene", name).c_str();
            if (ctx->assetManager->MoveScene(gScenesPanel.scene, target)) {
                SyncActiveScene(ctx, state);
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(PopupTitle(ScenesPopup::RenameFolder), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool bEnter = NameInput(ImGui::IsWindowAppearing());
        const Core::Path target = gScenesPanel.folder.Parent() / name;
        const bool bSame = target == gScenesPanel.folder;
        const bool bExists = !bNameEmpty && !bSame && Platform::FileExists(target);
        if (bExists) { ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Already exists"); }
        if (ConfirmButtons("Rename", !bNameEmpty && !bSame && !bExists, bEnter)) {
            if (!Platform::RenameFile(gScenesPanel.folder, target)) {
                LOG_ERROR(Engine, "Failed to rename folder '{}' to '{}'", gScenesPanel.folder.c_str(), target.c_str());
            }
            ctx->rescan.bScenes = true;
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(PopupTitle(ScenesPopup::DeleteScene), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        uint32_t runCount = 0;
        for (const auto& [id, run] : ctx->assetManager->GetPlayCache()) {
            if (run.sceneId == gScenesPanel.scene) { ++runCount; }
        }
        ImGui::Text("Move '%s' and its %u runs to the recycle bin?", name, runCount);
        if (ConfirmButtons("Delete", !IsLoaded(state, gScenesPanel.scene), false)) {
            ctx->assetManager->DeleteScene(gScenesPanel.scene);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(PopupTitle(ScenesPopup::ConfirmUnload), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("These scenes have unsaved changes:");
        for (StringID id : gScenesPanel.unloadIds) {
            if (!state->editor.modifiedScenes.Contains(id)) { continue; }
            const auto* meta = ctx->assetManager->GetSceneMetadata(id);
            ImGui::BulletText("%s", meta ? meta->sceneName.c_str() : "(unregistered)");
        }
        bool bUnload = false;
        ImGui::BeginDisabled(!ctx->bGameLoaded);
        if (ImGui::Button("Save and Unload")) {
            for (StringID id : gScenesPanel.unloadIds) {
                if (state->editor.modifiedScenes.Contains(id)) { SaveEditorScene(ctx, state, id); }
            }
            bUnload = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard and Unload")) { bUnload = true; }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            gScenesPanel.unloadIds.Clear();
            ImGui::CloseCurrentPopup();
        }
        if (bUnload) {
            UnloadScenes(state, gScenesPanel.unloadIds);
            gScenesPanel.unloadIds.Clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawScenesPanel(EngineContext* ctx, EngineState* state)
{
    bool& bOpen = state->editor.windowOpen[EDITOR_WINDOW_SCENES];
    if (!bOpen) { return; }
    if (ImGui::Begin(EDITOR_WINDOWS[EDITOR_WINDOW_SCENES].title, &bOpen)) {
        const Core::Path& root = Platform::GetScenePath();

        if (ImGui::Button("New Scene")) { Request(ScenesPopup::NewScene, {}, root, "new_scene"); }
        ImGui::SameLine();
        if (ImGui::Button("New Folder")) { Request(ScenesPopup::NewFolder, {}, root, "new_folder"); }
        ImGui::SameLine();

        Core::InlineVector<StringID, MAX_LOADED_SCENES> loaded;
        Core::InlineVector<StringID, MAX_LOADED_SCENES> others;
        for (const RuntimeSceneMetadata& scene : state->editor.loadedScenes) {
            loaded.PushBack(scene.sceneId);
            if (scene.sceneId != state->scene.currentSceneId) { others.PushBack(scene.sceneId); }
        }
        ImGui::BeginDisabled(loaded.IsEmpty());
        if (ImGui::Button("Unload All")) { RequestUnload(state, loaded); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(others.IsEmpty());
        if (ImGui::Button("Unload Others")) { RequestUnload(state, others); }
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled(!ctx->bGameLoaded);
        if (ImGui::Checkbox("Auto-save", &state->editor.bAutoSave)) {
            state->editor.autoSaveTimer = 0.0f;
        }
        ImGui::EndDisabled();
        if (state->editor.bAutoSave && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Auto-save in %.0fs", state->editor.autoSaveInterval - state->editor.autoSaveTimer);
        }
        if (!ctx->bGameLoaded) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Saving disabled: game.dll missing");
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##scene_search", "Search", gScenesPanel.search, sizeof(gScenesPanel.search));

        ImGui::BeginChild("##scene_tree");
        if (gScenesPanel.search[0] != '\0') {
            DrawSearchResults(ctx, state);
        }
        else {
            DrawFolder(ctx, state, root, true);
        }
        ImGui::EndChild();

        DrawPopups(ctx, state);
    }
    ImGui::End();
}
}
