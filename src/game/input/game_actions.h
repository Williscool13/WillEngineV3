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
} // Game::Actions

#endif //WILL_ENGINE_GAME_ACTIONS_H
