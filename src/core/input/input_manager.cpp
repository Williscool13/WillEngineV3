//
// Created by William on 2025-12-14.
//

#include "input_manager.h"

namespace Core
{
void InputManager::Init(const uint32_t w, const uint32_t h)
{
    this->windowExtents = glm::vec2(static_cast<float>(w), static_cast<float>(h));
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
            currentInput.mouseWheelDelta += event.wheel.mouse_y;
            break;
        }

        default:
            break;
    }
}

void InputManager::UpdateFocus(const Uint32 sdlWindowFlags)
{
    currentInput.isWindowInputFocus = (sdlWindowFlags & SDL_WINDOW_INPUT_FOCUS) != 0;
}

void InputManager::FrameReset()
{
    // Clear edge-triggered states
    for (auto& key : currentInput.keys) {
        key.pressed = false;
        key.released = false;
    }

    for (auto& btn : currentInput.mouseButtons) {
        btn.pressed = false;
        btn.released = false;
    }

    // Reset deltas
    currentInput.mouseXDelta = 0.0f;
    currentInput.mouseYDelta = 0.0f;
    currentInput.mouseWheelDelta = 0.0f;
}

void InputManager::UpdateWindowExtent(const uint32_t w, const uint32_t h)
{
    windowExtents = glm::vec2(static_cast<float>(w), static_cast<float>(h));
}

} // Core