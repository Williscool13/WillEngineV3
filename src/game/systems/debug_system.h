//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_DEBUG_SYSTEM_H
#define WILL_ENGINE_DEBUG_SYSTEM_H
#include <cstdint>
#include <vector>

#include "engine/material_manager.h"

namespace Render
{
struct FrameResources;
}

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
}

namespace Game
{
struct RenderDebugViewComponent
{
    uint32_t debugIndex;
};

struct DebugData
{
    std::vector<Engine::MaterialID> materials;
};
}

namespace Game::System
{
void DebugUpdate(Core::EngineContext* ctx, Engine::GameState* state);
void DebugProcessPhysicsCollisions(Core::EngineContext* ctx, Engine::GameState* state);
void DebugApplyGroundForces(Core::EngineContext* ctx, Engine::GameState* state);
} // Game::System

#endif //WILL_ENGINE_DEBUG_SYSTEM_H
