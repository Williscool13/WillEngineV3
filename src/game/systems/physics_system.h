//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_SYSTEM_H
#define WILL_ENGINE_PHYSICS_SYSTEM_H

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
void UpdatePhysics(Core::EngineContext* ctx, Engine::GameState* state);
} // Game

#endif //WILL_ENGINE_PHYSICS_SYSTEM_H