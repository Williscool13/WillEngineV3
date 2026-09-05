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
} // Game::Actions

#endif //WILL_ENGINE_GAME_ACTIONS_H
