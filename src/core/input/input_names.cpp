//
// Created by William on 2026-07-06.
//

#include "input_names.h"

#include <iterator>

namespace Core
{
static const char* const KEY_NAMES[] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Up", "Down", "Left", "Right",
    "L Ctrl", "L Shift", "L Alt", "L GUI",
    "R Ctrl", "R Shift", "R Alt", "R GUI",
    "Home", "End", "Page Up", "Page Down", "Insert", "Delete",
    "Escape", "Enter", "Backspace", "Tab", "Space",
    "Print Screen", "Scroll Lock", "Pause", "Caps Lock", "Num Lock",
    "Period", "Comma", "Semicolon", "Apostrophe",
    "Slash", "Backslash", "Minus", "Equals",
    "[", "]", "`",
    "Unknown"
};
static_assert(std::size(KEY_NAMES) == static_cast<size_t>(Key::COUNT), "KEY_NAMES must match Core::Key");

static const char* const MOUSE_BUTTON_NAMES[] = {"LMB", "MMB", "RMB", "Mouse 4", "Mouse 5"};
static_assert(std::size(MOUSE_BUTTON_NAMES) == static_cast<size_t>(MouseButton::COUNT), "MOUSE_BUTTON_NAMES must match Core::MouseButton");

static const char* const GAMEPAD_BUTTON_NAMES[] = {
    "South", "East", "West", "North",
    "Back", "Guide", "Start",
    "L Stick", "R Stick",
    "L Shoulder", "R Shoulder",
    "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right"
};
static_assert(std::size(GAMEPAD_BUTTON_NAMES) == static_cast<size_t>(GamepadButton::COUNT), "GAMEPAD_BUTTON_NAMES must match Core::GamepadButton");

const char* GetKeyName(Key k) { return KEY_NAMES[static_cast<size_t>(k)]; }
const char* GetMouseButtonName(MouseButton b) { return MOUSE_BUTTON_NAMES[static_cast<size_t>(b)]; }
const char* GetGamepadButtonName(GamepadButton b) { return GAMEPAD_BUTTON_NAMES[static_cast<size_t>(b)]; }
} // Core
