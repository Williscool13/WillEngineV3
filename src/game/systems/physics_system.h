//
// Created by William on 2025-12-25.
//

#ifndef WILL_ENGINE_PHYSICS_SYSTEM_H
#define WILL_ENGINE_PHYSICS_SYSTEM_H

namespace Core
{
struct FrameBuffer;
struct ViewFamily;
}

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
void DebugRenderPhysics(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);
} // Game

#endif //WILL_ENGINE_PHYSICS_SYSTEM_H