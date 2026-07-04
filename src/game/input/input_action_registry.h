//
// Created by William on 2026-07-04.
//

#ifndef WILL_ENGINE_INPUT_ACTION_REGISTRY_H
#define WILL_ENGINE_INPUT_ACTION_REGISTRY_H

namespace Engine
{
struct InputState;
}

namespace Game
{
void RegisterInputActions(Engine::InputState& input);
} // Game

#endif //WILL_ENGINE_INPUT_ACTION_REGISTRY_H
