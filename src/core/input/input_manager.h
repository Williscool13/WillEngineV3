//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_INPUT_MANAGER_H
#define WILL_ENGINE_INPUT_MANAGER_H


#include <SDL3/SDL_events.h>
#include "input_frame.h"
#include "input_utils.h"

namespace Core
{
class InputManager
{
public:
    InputManager() = default;

    InputManager(uint32_t w, uint32_t h);

    void ProcessEvent(const SDL_Event& event);

    void UpdateFocus(Uint32 sdlWindowFlags);

    void FrameReset();

    void UpdateWindowExtent(uint32_t w, uint32_t h);

    const InputFrame& GetCurrentInput() const { return currentInput; }

private:
    InputFrame currentInput{};
    glm::vec2 windowExtents{1700, 900};
};

} // Core

#endif //WILL_ENGINE_INPUT_MANAGER_H