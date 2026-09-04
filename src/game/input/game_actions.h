//
// Created by William on 2026-07-04.
//

#ifndef WILL_ENGINE_GAME_ACTIONS_H
#define WILL_ENGINE_GAME_ACTIONS_H

#include "core/string_id.h"
#include "engine/core/action_handle.h"

namespace Game::Actions
{
inline const Engine::ActionHandle ACTION_MOVE{"Move"_sid.id};
inline const Engine::ActionHandle ACTION_JUMP{"Jump"_sid.id};
inline const Engine::ActionHandle ACTION_LOOK{"Look"_sid.id};
inline const Engine::ActionHandle ACTION_LOOK_GAMEPAD{"Look_Gamepad"_sid.id};

inline const Engine::ActionHandle ACTION_DEBUG_PLAY_MUSIC{"Debug_PlayMusic"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_MUSIC_VOL_LOW{"Debug_MusicVolLow"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_MUSIC_VOL_FULL{"Debug_MusicVolFull"_sid.id};
inline const Engine::ActionHandle ACTION_SCREENSHOT{"Screenshot"_sid.id};
inline const Engine::ActionHandle ACTION_LOAD_LIGHTING_PROFILE_RESTIR{"LoadLightingProfile_ReSTIR"_sid.id};
inline const Engine::ActionHandle ACTION_LOAD_LIGHTING_PROFILE_STANDARD{"LoadLightingProfile_Standard"_sid.id};

inline const Engine::ActionHandle ACTION_EDITOR_CAM_LOOK_MODIFIER{"EditorCam_LookModifier"_sid.id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_PAN_MODIFIER{"EditorCam_PanModifier"_sid.id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_MOVE{"EditorCam_Move"_sid.id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_UP{"EditorCam_Up"_sid.id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_DOWN{"EditorCam_Down"_sid.id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_MOUSE_DELTA{"EditorCam_MouseDelta"_sid.id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_ZOOM_SPEED{"EditorCam_ZoomSpeed"_sid.id};

inline const Engine::ActionHandle ACTION_GIZMO_TRANSLATE{"Gizmo_Translate"_sid.id};
inline const Engine::ActionHandle ACTION_GIZMO_ROTATE{"Gizmo_Rotate"_sid.id};
inline const Engine::ActionHandle ACTION_GIZMO_SCALE{"Gizmo_Scale"_sid.id};
inline const Engine::ActionHandle ACTION_MODIFIER_CTRL{"Modifier_Ctrl"_sid.id};
inline const Engine::ActionHandle ACTION_MODIFIER_SHIFT{"Modifier_Shift"_sid.id};
inline const Engine::ActionHandle ACTION_DUPLICATE{"Duplicate"_sid.id};
inline const Engine::ActionHandle ACTION_DELETE_SELECTED{"DeleteSelected"_sid.id};
inline const Engine::ActionHandle ACTION_ESCAPE{"Escape"_sid.id};
inline const Engine::ActionHandle ACTION_BEGIN_RENAME{"BeginRename"_sid.id};
inline const Engine::ActionHandle ACTION_FOCUS_SELECTION{"FocusSelection"_sid.id};
inline const Engine::ActionHandle ACTION_VIEWPORT_SELECT{"ViewportSelect"_sid.id};
inline const Engine::ActionHandle ACTION_TOGGLE_CONSOLE{"ToggleConsole"_sid.id};

inline const Engine::ActionHandle ACTION_DEBUG_VIEW_1{"DebugView_1"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_2{"DebugView_2"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_3{"DebugView_3"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_4{"DebugView_4"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_5{"DebugView_5"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_6{"DebugView_6"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_7{"DebugView_7"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_8{"DebugView_8"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_9{"DebugView_9"_sid.id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_0{"DebugView_0"_sid.id};

inline const Engine::ActionHandle ACTION_SCENE_SLOT_1{"SceneSlot_1"_sid.id};
inline const Engine::ActionHandle ACTION_SCENE_SLOT_2{"SceneSlot_2"_sid.id};
inline const Engine::ActionHandle ACTION_SCENE_SLOT_3{"SceneSlot_3"_sid.id};
inline const Engine::ActionHandle ACTION_SCENE_SLOT_4{"SceneSlot_4"_sid.id};
inline const Engine::ActionHandle ACTION_SCENE_SLOT_5{"SceneSlot_5"_sid.id};
inline const Engine::ActionHandle ACTION_SCENE_SLOT_6{"SceneSlot_6"_sid.id};
inline const Engine::ActionHandle ACTION_SCENE_SLOT_7{"SceneSlot_7"_sid.id};
inline const Engine::ActionHandle ACTION_SCENE_SLOT_8{"SceneSlot_8"_sid.id};
inline const Engine::ActionHandle ACTION_SCENE_SLOT_9{"SceneSlot_9"_sid.id};

inline const Engine::ActionHandle ACTION_UI_POINTER_DOWN{"UI_PointerDown"_sid.id};
inline const Engine::ActionHandle ACTION_UI_SCROLL{"UI_Scroll"_sid.id};
inline const Engine::ActionHandle ACTION_UI_PAGE_UP{"UI_PageUp"_sid.id};
inline const Engine::ActionHandle ACTION_UI_PAGE_DOWN{"UI_PageDown"_sid.id};
} // Game::Actions

#endif //WILL_ENGINE_GAME_ACTIONS_H
