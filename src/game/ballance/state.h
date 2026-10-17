//
// Created by William on 2026-07-15.
//

#ifndef WILL_ENGINE_BALLANCE_STATE_H
#define WILL_ENGINE_BALLANCE_STATE_H

#include "game/ballance/player/physics_player_controller.h"

namespace Ballance
{
/**
 * Game-owned, hot-reload-persistent state.
 */
struct State
{
    /** Live only between PlayStart and PlayStop; GetCharacter() is null when no PIE player exists. */
    PhysicsPlayerController playerController;
};
} // Ballance

#endif //WILL_ENGINE_BALLANCE_STATE_H
