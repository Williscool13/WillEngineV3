//
// Created by William on 2026-07-15.
//

#ifndef WILL_ENGINE_GAME_STATE_H
#define WILL_ENGINE_GAME_STATE_H

#include "console/console.h"
#include "gameplay/player/physics_player_controller.h"
#include "systems/capture_shot_system.h"
#include "systems/probe_bake_system.h"

namespace Game
{
/**
 * Game-owned, hot-reload-persistent state.
 */
struct GameState
{
    Console::ConsoleState console;

    ProbeBakeSystem probeBake;
    CaptureShotSystem captureShot;

    /** Live only between PlayStart and PlayStop; GetCharacter() is null when no PIE player exists. */
    PhysicsPlayerController playerController;

    /** Consecutive frames with no asset or entity load in flight; reset to zero whenever anything is pending. Read by the MCP status tool. */
    int32_t quietFrames{0};
};
} // Game

#endif //WILL_ENGINE_GAME_STATE_H
