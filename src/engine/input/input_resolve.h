//
// Created by William on 2026-07-04.
//

#ifndef WILL_ENGINE_INPUT_RESOLVE_H
#define WILL_ENGINE_INPUT_RESOLVE_H

#include "core/input/input_frame.h"

namespace Engine
{
struct InputState;
enum class InputContext : uint8_t;

void ResolveInputActions(const Core::InputFrame& frame, InputContext context, InputState& input);
} // Engine

#endif //WILL_ENGINE_INPUT_RESOLVE_H
