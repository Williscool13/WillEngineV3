//
// Created by William on 2026-07-04.
//

#include "input_resolve.h"

#include "engine/input/input_binding.h"

namespace Engine
{
void ResolveInputActions(const Core::InputFrame& frame, InputContext context, InputState& input)
{
    input.mousePositionAbsolute = frame.mousePositionAbsolute;

    input.textInput.chars = frame.textInput;
    input.textInput.submit = frame.GetKey(Core::Key::RETURN).pressed;
    input.textInput.backspace = frame.GetKey(Core::Key::BACKSPACE).pressed;
    input.textInput.backspaceDown = frame.GetKey(Core::Key::BACKSPACE).down;
    input.textInput.deleteForward = frame.GetKey(Core::Key::DEL).pressed;
    input.textInput.deleteForwardDown = frame.GetKey(Core::Key::DEL).down;
    input.textInput.left = frame.GetKey(Core::Key::LEFT).pressed;
    input.textInput.leftDown = frame.GetKey(Core::Key::LEFT).down;
    input.textInput.right = frame.GetKey(Core::Key::RIGHT).pressed;
    input.textInput.rightDown = frame.GetKey(Core::Key::RIGHT).down;
    input.textInput.home = frame.GetKey(Core::Key::HOME).pressed;
    input.textInput.end = frame.GetKey(Core::Key::END).pressed;
    input.textInput.up = frame.GetKey(Core::Key::UP).pressed;
    input.textInput.down = frame.GetKey(Core::Key::DOWN).pressed;

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
                switch (src.type) {
                    case BindingSourceType::GamepadAxis: return frame.GetGamepadAxis(src.gamepadAxis);
                    case BindingSourceType::MouseDeltaX: return frame.mouseXDelta;
                    case BindingSourceType::MouseDeltaY: return frame.mouseYDelta;
                    case BindingSourceType::MouseWheelX: return frame.mouseWheelDelta.x;
                    case BindingSourceType::MouseWheelY: return frame.mouseWheelDelta.y;
                    default: return 0.0f;
                }
            };
            state.axis.x += AxisValue(binding.stick.x);
            state.axis.y += AxisValue(binding.stick.y);
        }
    }
}
} // Engine
