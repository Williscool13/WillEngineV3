//
// Created by William on 2026-09-29.
//

#include "editor_windows.h"

#include <cstring>

#include "imgui.h"
#include "engine/editor/editor_systems.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/project_config.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "engine/systems/scene_system.h"
#include "platform/file_utils.h"
#include "platform/paths.h"

namespace Engine
{
static Core::Path GetEditorConfigPath()
{
    return Platform::GetConfigPath() / "editor.wconfig";
}

void ReadEditorWindowConfig(EngineState* state)
{
    bool* open = state->editor.windowOpen;
    for (uint32_t i = 0; i < EDITOR_WINDOW_COUNT; ++i) {
        open[i] = EDITOR_WINDOWS[i].bDefaultOpen;
    }

    Platform::ScopedFileMapping map(GetEditorConfigPath());
    if (map.data) {
        const TextReader windows = TextReader(map.data, map.size).Block("windows");
        for (uint32_t i = 0; i < EDITOR_WINDOW_COUNT; ++i) {
            open[i] = windows.Bool(EDITOR_WINDOWS[i].key, open[i]);
        }
    }
    memcpy(state->editor.windowOpenSaved, open, sizeof(state->editor.windowOpenSaved));
}

static void WriteEditorWindowConfig(EngineState* state)
{
    Core::Vector<std::byte> body(state->allocator, Core::AllocTag::EngineState);
    TextWriter w(body);
    w.BeginBlock("windows");
    for (uint32_t i = 0; i < EDITOR_WINDOW_COUNT; ++i) {
        w.Key(EDITOR_WINDOWS[i].key, state->editor.windowOpen[i]);
    }
    w.EndBlock();
    Platform::WriteFile(GetEditorConfigPath(), body.Data(), body.Size());
    memcpy(state->editor.windowOpenSaved, state->editor.windowOpen, sizeof(state->editor.windowOpenSaved));
}

static bool IsSceneLoaded(const EngineState* state, StringID sceneId)
{
    for (const RuntimeSceneMetadata& scene : state->editor.loadedScenes) {
        if (scene.sceneId == sceneId) { return true; }
    }
    return false;
}

static void DrawFileMenu(EngineContext* ctx, EngineState* state)
{
    const StringID current = state->scene.currentSceneId;
    const bool bCanSave = ctx->bGameLoaded && !IsPlaying(state);

    ImGui::BeginDisabled(!bCanSave || !IsSceneLoaded(state, current));
    if (ImGui::MenuItem(Core::InlineString<160>::Format("Save '%s'", state->scene.currentSceneName.c_str()).c_str(), "Ctrl+S")) {
        SaveEditorScene(ctx, state, current);
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!bCanSave || state->editor.modifiedScenes.IsEmpty());
    if (ImGui::MenuItem("Save All", "Ctrl+Shift+S")) {
        SaveModifiedScenes(ctx, state);
    }
    ImGui::EndDisabled();
    if (!ctx->bGameLoaded && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Saving disabled: game.dll missing");
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!current.IsValid());
    if (ImGui::MenuItem("Set as Default Scene", nullptr, current.IsValid() && state->projectConfig.defaultScene == current)) {
        state->projectConfig.defaultScene = current;
        WriteProjectConfig(state->projectConfig, state->allocator);
    }
    ImGui::EndDisabled();
}

static void DrawEditMenu(EngineState* state)
{
    UndoStack& undo = state->editor.undo;
    const bool bEditing = state->inputContext == InputContext::Editor;

    ImGui::BeginDisabled(!bEditing || !undo.CanUndo());
    if (ImGui::MenuItem("Undo", "Ctrl+Z")) { undo.Undo(state); }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!bEditing || !undo.CanRedo());
    if (ImGui::MenuItem("Redo", "Ctrl+Y")) { undo.Redo(state); }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::MenuItem(EDITOR_WINDOWS[EDITOR_WINDOW_UNDO_HISTORY].title, nullptr, &state->editor.windowOpen[EDITOR_WINDOW_UNDO_HISTORY]);
}

static void DrawWindowMenu(EngineState* state)
{
    const char* group = EDITOR_WINDOWS[0].group;
    for (uint32_t i = 0; i < EDITOR_WINDOW_COUNT; ++i) {
        if (strcmp(group, EDITOR_WINDOWS[i].group) != 0) {
            group = EDITOR_WINDOWS[i].group;
            ImGui::Separator();
        }
        ImGui::MenuItem(EDITOR_WINDOWS[i].title, nullptr, &state->editor.windowOpen[i]);
    }
}

void DrawEditorMenuBar(EngineContext* ctx, EngineState* state)
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            DrawFileMenu(ctx, state);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            DrawEditMenu(state);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window")) {
            DrawWindowMenu(state);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (memcmp(state->editor.windowOpen, state->editor.windowOpenSaved, sizeof(state->editor.windowOpenSaved)) != 0) {
        WriteEditorWindowConfig(state);
    }
}
} // Engine
