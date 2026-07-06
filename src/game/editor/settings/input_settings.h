//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_INPUT_SETTINGS_H
#define WILL_ENGINE_INPUT_SETTINGS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game
{
void DrawInputBindingsWindow(Engine::EngineContext* ctx, Engine::EngineState* state);
} // Game

#endif //WILL_ENGINE_INPUT_SETTINGS_H
