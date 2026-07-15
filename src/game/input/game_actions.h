//
// Created by William on 2026-07-04.
//

#ifndef WILL_ENGINE_GAME_ACTIONS_H
#define WILL_ENGINE_GAME_ACTIONS_H

#include "core/string_id.h"
#include "engine/core/action_handle.h"

namespace Game::Actions
{
inline const Engine::ActionHandle ACTION_MOVE{SID("Move").id};
inline const Engine::ActionHandle ACTION_JUMP{SID("Jump").id};
inline const Engine::ActionHandle ACTION_LOOK{SID("Look").id};
inline const Engine::ActionHandle ACTION_LOOK_GAMEPAD{SID("Look_Gamepad").id};

inline const Engine::ActionHandle ACTION_DEBUG_PLAY_MUSIC{SID("Debug_PlayMusic").id};
inline const Engine::ActionHandle ACTION_DEBUG_MUSIC_VOL_LOW{SID("Debug_MusicVolLow").id};
inline const Engine::ActionHandle ACTION_DEBUG_MUSIC_VOL_FULL{SID("Debug_MusicVolFull").id};
inline const Engine::ActionHandle ACTION_SCREENSHOT{SID("Screenshot").id};
inline const Engine::ActionHandle ACTION_LOAD_LIGHTING_PROFILE_RESTIR{SID("LoadLightingProfile_ReSTIR").id};
inline const Engine::ActionHandle ACTION_LOAD_LIGHTING_PROFILE_STANDARD{SID("LoadLightingProfile_Standard").id};

inline const Engine::ActionHandle ACTION_EDITOR_CAM_LOOK_MODIFIER{SID("EditorCam_LookModifier").id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_PAN_MODIFIER{SID("EditorCam_PanModifier").id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_MOVE{SID("EditorCam_Move").id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_UP{SID("EditorCam_Up").id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_DOWN{SID("EditorCam_Down").id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_MOUSE_DELTA{SID("EditorCam_MouseDelta").id};
inline const Engine::ActionHandle ACTION_EDITOR_CAM_ZOOM_SPEED{SID("EditorCam_ZoomSpeed").id};

inline const Engine::ActionHandle ACTION_GIZMO_TRANSLATE{SID("Gizmo_Translate").id};
inline const Engine::ActionHandle ACTION_GIZMO_ROTATE{SID("Gizmo_Rotate").id};
inline const Engine::ActionHandle ACTION_GIZMO_SCALE{SID("Gizmo_Scale").id};
inline const Engine::ActionHandle ACTION_MODIFIER_CTRL{SID("Modifier_Ctrl").id};
inline const Engine::ActionHandle ACTION_MODIFIER_SHIFT{SID("Modifier_Shift").id};
inline const Engine::ActionHandle ACTION_DUPLICATE{SID("Duplicate").id};
inline const Engine::ActionHandle ACTION_DELETE_SELECTED{SID("DeleteSelected").id};
inline const Engine::ActionHandle ACTION_ESCAPE{SID("Escape").id};
inline const Engine::ActionHandle ACTION_BEGIN_RENAME{SID("BeginRename").id};
inline const Engine::ActionHandle ACTION_FOCUS_SELECTION{SID("FocusSelection").id};
inline const Engine::ActionHandle ACTION_VIEWPORT_SELECT{SID("ViewportSelect").id};
inline const Engine::ActionHandle ACTION_TOGGLE_CONSOLE{SID("ToggleConsole").id};

inline const Engine::ActionHandle ACTION_DEBUG_VIEW_1{SID("DebugView_1").id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_2{SID("DebugView_2").id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_3{SID("DebugView_3").id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_4{SID("DebugView_4").id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_5{SID("DebugView_5").id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_6{SID("DebugView_6").id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_7{SID("DebugView_7").id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_8{SID("DebugView_8").id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_9{SID("DebugView_9").id};
inline const Engine::ActionHandle ACTION_DEBUG_VIEW_0{SID("DebugView_0").id};

inline const Engine::ActionHandle ACTION_UI_POINTER_DOWN{SID("UI_PointerDown").id};
inline const Engine::ActionHandle ACTION_UI_SCROLL{SID("UI_Scroll").id};
inline const Engine::ActionHandle ACTION_UI_PAGE_UP{SID("UI_PageUp").id};
inline const Engine::ActionHandle ACTION_UI_PAGE_DOWN{SID("UI_PageDown").id};
} // Game::Actions

#endif //WILL_ENGINE_GAME_ACTIONS_H
