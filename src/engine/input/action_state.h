//
// Created by William on 2026-07-04.
//

#ifndef WILL_ENGINE_ACTION_STATE_H
#define WILL_ENGINE_ACTION_STATE_H

#include "core/types/math.h"

namespace Core
{
/**
 * `pressed` and `released` are only safe to read from code called off GameUpdate (tick-cadence).
 * Code called off GamePrepareFrame should only read down/axis, or a value already latched during GameUpdate.
 */
struct ActionState
{
    bool pressed{false};
    bool down{false};
    bool released{false};
    Vec2 axis{};
};
} // Core

#endif //WILL_ENGINE_ACTION_STATE_H
