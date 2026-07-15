//
// Created by William on 2026-07-15.
//

#ifndef WILL_ENGINE_GAME_STATE_H
#define WILL_ENGINE_GAME_STATE_H

#include "console/console.h"

namespace Game
{
/**
 * Game-owned, hot-reload-persistent state.
 */
struct GameState
{
    Console::ConsoleState console;
};
} // Game

#endif //WILL_ENGINE_GAME_STATE_H
