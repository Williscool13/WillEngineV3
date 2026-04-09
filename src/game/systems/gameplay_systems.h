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
struct EngineState;
}

namespace Game
{
void UpdatePathMovers(Core::EngineContext* ctx, Engine::EngineState* state);
void CheckpointUpdate(Core::EngineContext* ctx, Engine::EngineState* state);
void DeathZoneUpdate(Core::EngineContext* ctx, Engine::EngineState* state);
} // Game

#endif //WILL_ENGINE_GAMEPLAY_SYSTEMS_H
