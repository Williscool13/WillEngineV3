//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_INPUT_FRAME_H
#define WILL_ENGINE_INPUT_FRAME_H

#include <cstdint>
#include "core/containers/array.h"
#include "core/containers/inline_string.h"
#include "core/types/math.h"

namespace Core
{
// Dense sequential enum for array indexing
enum class Key : uint32_t
{
    A = 0, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    NUM_0, NUM_1, NUM_2, NUM_3, NUM_4, NUM_5, NUM_6, NUM_7, NUM_8, NUM_9,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    UP, DOWN, LEFT, RIGHT,

    LCTRL, LSHIFT, LALT, LGUI,
    RCTRL, RSHIFT, RALT, RGUI,

    HOME, END, PAGEUP, PAGEDOWN, INSERT, DEL,

    ESCAPE, RETURN, BACKSPACE, TAB, SPACE,
    PRINTSCREEN, SCROLLLOCK, PAUSE, CAPSLOCK, NUMLOCKCLEAR,

    PERIOD, COMMA, SEMICOLON, APOSTROPHE,
    SLASH, BACKSLASH, MINUS, EQUALS,
    LEFTBRACKET, RIGHTBRACKET, BACKTICK,

    UNKNOWN,
    COUNT
};

enum class MouseButton : uint8_t
{
    LMB = 0,
    MMB,
    RMB,
    X1,
    X2,
    COUNT
};

// Dense sequential enum for array indexing; maps 1:1 onto SDL_GamepadButton
enum class GamepadButton : uint8_t
{
    SOUTH = 0, EAST, WEST, NORTH,
    BACK, GUIDE, START,
    LEFT_STICK, RIGHT_STICK,
    LEFT_SHOULDER, RIGHT_SHOULDER,
    DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT,
    COUNT
};

// Dense sequential enum for array indexing; maps 1:1 onto SDL_GamepadAxis
enum class GamepadAxis : uint8_t
{
    LEFT_X = 0, LEFT_Y,
    RIGHT_X, RIGHT_Y,
    LEFT_TRIGGER, RIGHT_TRIGGER,
    COUNT
};

struct InputFrame
{
    struct ButtonState
    {
        bool pressed;
        bool down;
        bool released;
    };

    Array<ButtonState, static_cast<size_t>(Key::COUNT)> keys{};
    Array<ButtonState, static_cast<size_t>(MouseButton::COUNT)> mouseButtons{};
    Array<ButtonState, static_cast<size_t>(GamepadButton::COUNT)> gamepadButtons{};

    /**
     * Normalized [-1, 1] for sticks, [0, 1] for triggers
     */
    Array<float, static_cast<size_t>(GamepadAxis::COUNT)> gamepadAxes{};
    bool isGamepadConnected{false};

    /**
     * Normalized mouse position
     */
    Vec2 mousePosition{};
    Vec2 mousePositionAbsolute{};
    float mouseXDelta{0.0f};
    float mouseYDelta{0.0f};
    Vec2 mouseWheelDelta{};

    // UTF-8 characters typed this frame (SDL_EVENT_TEXT_INPUT)
    InlineString<32> textInput{};

    bool isCursorActive{false};
    bool isWindowInputFocus{false};

    [[nodiscard]] const ButtonState& GetKey(Key k) const { return keys[static_cast<size_t>(k)]; }
    [[nodiscard]] const ButtonState& GetMouse(MouseButton btn) const { return mouseButtons[static_cast<size_t>(btn)]; }
    [[nodiscard]] const ButtonState& GetGamepadButton(GamepadButton btn) const { return gamepadButtons[static_cast<size_t>(btn)]; }
    [[nodiscard]] float GetGamepadAxis(GamepadAxis axis) const { return gamepadAxes[static_cast<size_t>(axis)]; }

    ButtonState& GetKey(Key k) { return keys[static_cast<size_t>(k)]; }
    ButtonState& GetMouse(MouseButton btn) { return mouseButtons[static_cast<size_t>(btn)]; }
    ButtonState& GetGamepadButton(GamepadButton btn) { return gamepadButtons[static_cast<size_t>(btn)]; }

    bool ConsumeKeyPress(Key key)
    {
        auto& state = keys[static_cast<size_t>(key)];
        bool wasPressed = state.pressed;
        state.pressed = false;
        return wasPressed;
    }

    bool ConsumeMousePress(MouseButton button)
    {
        auto& state = mouseButtons[static_cast<size_t>(button)];
        bool wasPressed = state.pressed;
        state.pressed = false;
        return wasPressed;
    }
};
} // Core

using InputFrame = Core::InputFrame;
using Key = Core::Key;
using MouseButton = Core::MouseButton;
using GamepadButton = Core::GamepadButton;
using GamepadAxis = Core::GamepadAxis;

#endif //WILL_ENGINE_INPUT_FRAME_H
