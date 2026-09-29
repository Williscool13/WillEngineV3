//
// Created by William on 2026-09-29.
//

#ifndef WILL_ENGINE_EDITOR_WINDOWS_H
#define WILL_ENGINE_EDITOR_WINDOWS_H

#include <cstdint>

namespace Engine
{
struct EngineContext;
struct EngineState;

enum EditorWindow : uint32_t
{
    EDITOR_WINDOW_TOOLBAR,
    EDITOR_WINDOW_SCENES,
    EDITOR_WINDOW_OUTLINER,
    EDITOR_WINDOW_DETAILS,
    EDITOR_WINDOW_SPAWN,
    EDITOR_WINDOW_SCENE_STATS,
    EDITOR_WINDOW_GAMEPLAY,
    EDITOR_WINDOW_LIGHTING,
    EDITOR_WINDOW_POST_PROCESSING,
    EDITOR_WINDOW_DEBUG_VIEW,
    EDITOR_WINDOW_DIAGNOSTICS,
    EDITOR_WINDOW_MATERIALS,
    EDITOR_WINDOW_TEXTURES,
    EDITOR_WINDOW_PROJECT_CONFIG,
    EDITOR_WINDOW_INPUT_BINDINGS,
    EDITOR_WINDOW_UNDO_HISTORY,
    EDITOR_WINDOW_EDITOR,
    EDITOR_WINDOW_COUNT,
};

struct EditorWindowInfo
{
    const char* title;
    const char* key;
    const char* group;
    bool bDefaultOpen;
};

inline constexpr EditorWindowInfo EDITOR_WINDOWS[EDITOR_WINDOW_COUNT] = {
    {"Toolbar", "toolbar", "Scene", true},
    {"Scenes", "scenes", "Scene", true},
    {"Outliner", "outliner", "Scene", true},
    {"Details", "details", "Scene", true},
    {"Spawn", "spawn", "Scene", true},
    {"Scene Stats", "scene_stats", "Scene", true},
    {"Gameplay", "gameplay", "Scene", true},
    {"Lighting", "lighting", "Rendering", true},
    {"Post-Processing", "post_processing", "Rendering", true},
    {"Debug View", "debug_view", "Rendering", true},
    {"Diagnostics", "diagnostics", "Rendering", true},
    {"Materials", "materials", "Assets", true},
    {"Textures", "textures", "Assets", true},
    {"Project Config", "project_config", "Settings", true},
    {"Input Bindings", "input_bindings", "Settings", true},
    {"Undo History", "undo_history", "Tools", false},
    {"Editor", "editor", "Tools", true},
};

void ReadEditorWindowConfig(EngineState* state);

/** Call before the dockspace. */
void DrawEditorMenuBar(EngineContext* ctx, EngineState* state);
} // Engine

#endif //WILL_ENGINE_EDITOR_WINDOWS_H
