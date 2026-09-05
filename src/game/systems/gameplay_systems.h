//
// Created by William on 2026-03-26.
//

#ifndef WILL_ENGINE_GAMEPLAY_SYSTEMS_H
#define WILL_ENGINE_GAMEPLAY_SYSTEMS_H

#include <entt/entt.hpp>

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game
{
void ConnectGameplayObservers(entt::registry& registry);
void DisconnectGameplayObservers(entt::registry& registry);

void UpdatePathMovers(Engine::EngineContext* ctx, Engine::EngineState* state);
void UpdateRotateInPlace(Engine::EngineContext* ctx, Engine::EngineState* state);
void CheckpointUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);
void DeathZoneUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);
} // Game

#endif //WILL_ENGINE_GAMEPLAY_SYSTEMS_H
