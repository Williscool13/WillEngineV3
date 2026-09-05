//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_INPUT_MANAGER_H
#define WILL_ENGINE_INPUT_MANAGER_H


#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include "input_frame.h"
#include "input_utils.h"

namespace Core
{
class InputManager
{
public:
    InputManager() = default;

    InputManager(uint32_t w, uint32_t h);

    ~InputManager();

    void ProcessEvent(const SDL_Event& event);

    void UpdateFocus(Uint32 sdlWindowFlags);

    void FrameReset();

    void UpdateWindowExtent(uint32_t w, uint32_t h);

    [[nodiscard]] const InputFrame& GetCurrentInput() const { return currentInput; }

    [[nodiscard]] bool IsQuitRequested() const { return bRequestedQuit; }

private:
    void OpenFirstGamepad();
    void CloseGamepad();

    InputFrame currentInput{};
    bool bRequestedQuit{false};
    Vec2 windowExtents{1700, 900};

    SDL_Gamepad* gamepad{nullptr};
    static constexpr float GAMEPAD_DEADZONE = 0.15f;
};
} // Core

#endif //WILL_ENGINE_INPUT_MANAGER_H
