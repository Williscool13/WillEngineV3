//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_DEBUG_SYSTEM_H
#define WILL_ENGINE_DEBUG_SYSTEM_H
#include "engine/asset_manager_types.h"

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
}

namespace Game::System
{
void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state);

void DebugPrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);
} // Game::System

#endif //WILL_ENGINE_DEBUG_SYSTEM_H
