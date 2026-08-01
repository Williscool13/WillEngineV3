//
// Created by William on 2026-07-06.
//

#include "input_rebinding.h"

#include "engine/input/input_binding.h"

namespace Engine
{
Core::InlineVector<size_t, 8> EnumerateBindingRows(const InputState& input, ActionHandle action)
{
    Core::InlineVector<size_t, 8> rows;
    for (size_t i = 0; i < input.bindings.Size(); ++i) {
        if (input.bindings[i].action == action) {
            rows.PushBack(i);
        }
    }
    return rows;
}

void BeginCapture(InputState& input, size_t bindingRow)
{
    input.bCaptureActive = true;
    input.captureTargetBindingRow = bindingRow;
}

void PollCapture(InputState& input, const Core::InputFrame& frame)
{
    if (!input.bCaptureActive) { return; }

    const auto Finish = [&input]() {
        input.bCaptureActive = false;
        input.captureTargetBindingRow = ~size_t{0};
    };

    if (frame.GetKey(Key::ESCAPE).pressed) {
        Finish();
        return;
    }

    for (size_t i = 0; i < static_cast<size_t>(Key::COUNT); ++i) {
        if (frame.keys[i].pressed) {
            RewriteBinding(input, input.captureTargetBindingRow, BindingSource::FromKey(static_cast<Core::Key>(i)));
            Finish();
            return;
        }
    }
    for (size_t i = 0; i < static_cast<size_t>(MouseButton::COUNT); ++i) {
        if (frame.mouseButtons[i].pressed) {
            RewriteBinding(input, input.captureTargetBindingRow, BindingSource::FromMouse(static_cast<Core::MouseButton>(i)));
            Finish();
            return;
        }
    }
    for (size_t i = 0; i < static_cast<size_t>(GamepadButton::COUNT); ++i) {
        if (frame.gamepadButtons[i].pressed) {
            RewriteBinding(input, input.captureTargetBindingRow, BindingSource::FromGamepadButton(static_cast<Core::GamepadButton>(i)));
            Finish();
            return;
        }
    }
}

void RewriteBinding(InputState& input, size_t bindingRow, BindingSource newSource)
{
    if (bindingRow >= input.bindings.Size()) { return; }
    ActionBinding& binding = input.bindings[bindingRow];
    if (binding.shape != BindingShape::Discrete) { return; }
    binding.source = newSource;
    input.bBindingsDirty = true;
}

void ResetBindingToDefault(InputState& input, size_t bindingRow)
{
    if (bindingRow >= input.bindings.Size() || bindingRow >= input.defaultBindings.Size()) { return; }
    input.bindings[bindingRow] = input.defaultBindings[bindingRow];
    input.bBindingsDirty = true;
}

void ApplyDefaultBindings(InputState& input)
{
    input.bindings = input.defaultBindings;
}
} // Engine
