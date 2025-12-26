//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_DEBUG_SYSTEM_H
#define WILL_ENGINE_DEBUG_SYSTEM_H

namespace Engine
{
struct GameState;
}

namespace Core
{
struct EngineContext;
}

namespace Game::System
{
void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state);
void DebugProcessPhysicsCollisions(Core::EngineContext* ctx, Engine::GameState* state);
void DebugApplyGroundForces(Core::EngineContext* ctx, Engine::GameState* state);
} // Game::System

#endif //WILL_ENGINE_DEBUG_SYSTEM_H
