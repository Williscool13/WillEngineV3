//
// Created by William on 2026-03-26.
//

#ifndef WILL_ENGINE_GAMEPLAY_SYSTEMS_H
#define WILL_ENGINE_GAMEPLAY_SYSTEMS_H

namespace Core
{
struct EngineContext;
}

namespace Engine
{
struct GameState;
}

namespace Game
{
void UpdatePathMovers(Core::EngineContext* ctx, Engine::GameState* state);
} // Game

#endif //WILL_ENGINE_GAMEPLAY_SYSTEMS_H
