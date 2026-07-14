//
// Created by William on 2025-12-14.
//

#include "input_manager.h"

namespace Core
{
InputManager::InputManager(uint32_t w, uint32_t h)
{
    windowExtents = Vec2(static_cast<float>(w), static_cast<float>(h));
    OpenFirstGamepad();
}

InputManager::~InputManager()
{
    if (gamepad) {
        SDL_CloseGamepad(gamepad);
    }
}

void InputManager::ProcessEvent(const SDL_Event& event)
{
    switch (event.type)
    {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            Key key = SDLKeyToKey(event.key.key);
            if (key != Key::UNKNOWN) {
                UpdateButtonState(currentInput.GetKey(key), event.key.down);
            }
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
        {
            currentInput.textInput.Append(event.text.text);
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            MouseButton btn = SDLMouseButtonToMouseButton(event.button.button);
            UpdateButtonState(currentInput.GetMouse(btn), event.button.down);
            break;
        }

        case SDL_EVENT_MOUSE_MOTION:
        {
            currentInput.mouseXDelta += static_cast<float>(event.motion.xrel);
            currentInput.mouseYDelta += static_cast<float>(event.motion.yrel);
            currentInput.mousePositionAbsolute = {event.motion.x, event.motion.y};
            currentInput.mousePosition = currentInput.mousePositionAbsolute / windowExtents;
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL:
        {
            currentInput.mouseWheelDelta.x += event.wheel.x;
            currentInput.mouseWheelDelta.y += event.wheel.y;
            break;
        }

        case SDL_EVENT_GAMEPAD_ADDED:
        {
            if (!gamepad) {
                OpenFirstGamepad();
            }
            break;
        }

        case SDL_EVENT_GAMEPAD_REMOVED:
        {
            if (gamepad && SDL_GetGamepadID(gamepad) == event.gdevice.which) {
                CloseGamepad();
            }
            break;
        }

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        {
            GamepadButton btn = SDLGamepadButtonToGamepadButton(static_cast<SDL_GamepadButton>(event.gbutton.button));
            if (btn != GamepadButton::COUNT) {
                UpdateButtonState(currentInput.GetGamepadButton(btn), event.gbutton.down);
            }
            break;
        }

        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        {
            GamepadAxis axis;
            switch (static_cast<SDL_GamepadAxis>(event.gaxis.axis))
            {
                case SDL_GAMEPAD_AXIS_LEFTX: axis = GamepadAxis::LEFT_X; break;
                case SDL_GAMEPAD_AXIS_LEFTY: axis = GamepadAxis::LEFT_Y; break;
                case SDL_GAMEPAD_AXIS_RIGHTX: axis = GamepadAxis::RIGHT_X; break;
                case SDL_GAMEPAD_AXIS_RIGHTY: axis = GamepadAxis::RIGHT_Y; break;
                case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: axis = GamepadAxis::LEFT_TRIGGER; break;
                case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: axis = GamepadAxis::RIGHT_TRIGGER; break;
                default: axis = GamepadAxis::COUNT; break;
            }
            if (axis != GamepadAxis::COUNT) {
                currentInput.gamepadAxes[static_cast<size_t>(axis)] = NormalizeGamepadAxis(axis, event.gaxis.value, GAMEPAD_DEADZONE);
            }
            break;
        }

        case SDL_EVENT_QUIT:
        {
            bRequestedQuit = true;
            break;
        }

        default:
            break;
    }
}

void InputManager::OpenFirstGamepad()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0) {
        gamepad = SDL_OpenGamepad(ids[0]);
        currentInput.isGamepadConnected = gamepad != nullptr;
    }
    SDL_free(ids);
}

void InputManager::CloseGamepad()
{
    SDL_CloseGamepad(gamepad);
    gamepad = nullptr;
    currentInput.isGamepadConnected = false;

    for (auto& btn : currentInput.gamepadButtons) {
        btn = {};
    }
    for (float& axis : currentInput.gamepadAxes) {
        axis = 0.0f;
    }
}

void InputManager::UpdateFocus(const Uint32 sdlWindowFlags)
{
    currentInput.isWindowInputFocus = (sdlWindowFlags & SDL_WINDOW_INPUT_FOCUS) != 0;
}

void InputManager::FrameReset()
{
    for (auto& key : currentInput.keys) {
        key.pressed = false;
        key.released = false;
    }

    for (auto& btn : currentInput.mouseButtons) {
        btn.pressed = false;
        btn.released = false;
    }

    for (auto& btn : currentInput.gamepadButtons) {
        btn.pressed = false;
        btn.released = false;
    }

    currentInput.mouseXDelta = 0.0f;
    currentInput.mouseYDelta = 0.0f;
    currentInput.mouseWheelDelta.x = 0;
    currentInput.mouseWheelDelta.y = 0;
    currentInput.textInput.Clear();
}

void InputManager::UpdateWindowExtent(const uint32_t w, const uint32_t h)
{
    windowExtents = Vec2(static_cast<float>(w), static_cast<float>(h));
}

} // Core