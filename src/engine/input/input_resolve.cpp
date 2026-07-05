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

        const auto GetButtonState = [&frame](const BindingSource& src) -> const Core::InputFrame::ButtonState& {
            switch (src.type) {
                case BindingSourceType::Key: return frame.GetKey(src.key);
                case BindingSourceType::MouseButton: return frame.GetMouse(src.mouseButton);
                case BindingSourceType::GamepadButton: return frame.GetGamepadButton(src.gamepadButton);
                default: return frame.GetKey(Core::Key::UNKNOWN);
            }
        };

        if (binding.shape == BindingShape::Discrete) {
            const Core::InputFrame::ButtonState& btn = GetButtonState(binding.source);
            state.pressed |= btn.pressed;
            state.down |= btn.down;
            state.released |= btn.released;
        }
        else if (binding.shape == BindingShape::Axis2DComposite) {
            const auto Down = [&GetButtonState](const BindingSource& src) -> bool { return GetButtonState(src).down; };
            state.axis.x += (Down(binding.composite.right) ? 1.0f : 0.0f) - (Down(binding.composite.left) ? 1.0f : 0.0f);
            state.axis.y += (Down(binding.composite.up) ? 1.0f : 0.0f) - (Down(binding.composite.down) ? 1.0f : 0.0f);
        }
        else {
            const auto AxisValue = [&frame](const BindingSource& src) -> float {
                return src.type == BindingSourceType::GamepadAxis ? frame.GetGamepadAxis(src.gamepadAxis) : 0.0f;
            };
            state.axis.x += AxisValue(binding.stick.x);
            state.axis.y += AxisValue(binding.stick.y);
        }
    }
}
} // Engine
