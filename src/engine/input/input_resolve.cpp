//
// Created by William on 2026-07-04.
//

#include "input_resolve.h"

#include "engine/engine_api.h"

namespace Engine
{
void ResolveInputActions(const Core::InputFrame& frame, InputContext context, InputState& input)
{
    if (input.bCaptureActive) { return; }

    for (Core::ActionState& state : input.actionStates) {
        state.pressed = false;
        state.down = false;
        state.released = false;
        state.axis = {};
    }

    for (const ActionBinding& binding : input.bindings) {
        if (binding.context != context) { continue; }

        const size_t* idx = input.actionIndex.Find(binding.action);
        if (!idx) { continue; }
        Core::ActionState& state = input.actionStates[*idx];

        if (binding.shape == BindingShape::Discrete) {
            const Core::InputFrame::ButtonState& btn = binding.source.type == BindingSourceType::Key ? frame.GetKey(binding.source.key) : frame.GetMouse(binding.source.mouseButton);
            state.pressed |= btn.pressed;
            state.down |= btn.down;
            state.released |= btn.released;
        }
        else {
            const auto Down = [&frame](const BindingSource& src) -> bool {
                return src.type == BindingSourceType::Key ? frame.GetKey(src.key).down : frame.GetMouse(src.mouseButton).down;
            };
            state.axis.x += (Down(binding.composite.right) ? 1.0f : 0.0f) - (Down(binding.composite.left) ? 1.0f : 0.0f);
            state.axis.y += (Down(binding.composite.up) ? 1.0f : 0.0f) - (Down(binding.composite.down) ? 1.0f : 0.0f);
        }
    }
}
} // Engine
