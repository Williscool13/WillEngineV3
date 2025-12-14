//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_INPUT_FRAME_H
#define WILL_ENGINE_INPUT_FRAME_H

#include <cstdint>
#include <glm/glm.hpp>

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

    HOME, END, PAGEUP, PAGEDOWN, INSERT,

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

struct InputFrame
{
    struct ButtonState
    {
        bool pressed;
        bool down;
        bool released;
    };

    ButtonState keys[static_cast<size_t>(Key::COUNT)]{};
    ButtonState mouseButtons[static_cast<size_t>(MouseButton::COUNT)]{};

    /**
     * Normalized mouse position
     */
    glm::vec2 mousePosition{};
    glm::vec2 mousePositionAbsolute{};
    float mouseXDelta{0.0f};
    float mouseYDelta{0.0f};
    float mouseWheelDelta{0.0f};

    bool isCursorActive{false};
    bool isWindowInputFocus{false};

    // Helper accessors
    const ButtonState& GetKey(Key k) const { return keys[static_cast<size_t>(k)]; }
    const ButtonState& GetMouse(MouseButton btn) const { return mouseButtons[static_cast<size_t>(btn)]; }

    ButtonState& GetKey(Key k) { return keys[static_cast<size_t>(k)]; }
    ButtonState& GetMouse(MouseButton btn) { return mouseButtons[static_cast<size_t>(btn)]; }
};
} // Core

using InputFrame = Core::InputFrame;
using Key = Core::Key;
using MouseButton = Core::MouseButton;

#endif //WILL_ENGINE_INPUT_FRAME_H
