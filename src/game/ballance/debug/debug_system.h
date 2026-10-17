//
// Created by William on 2025-12-22.
//

#ifndef WILL_ENGINE_BALLANCE_DEBUG_SYSTEM_H
#define WILL_ENGINE_BALLANCE_DEBUG_SYSTEM_H

#include "render/interface/render_interface.h"
#include "engine/editor/debug_hotkeys.h"

namespace Render
{
struct FrameResources;
}

namespace Engine
{
struct EngineState;
}

namespace Core
{
struct FrameBuffer;
}

namespace Ballance
{
void DebugUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);
void DebugProcessPhysicsCollisions(Engine::EngineContext* ctx, Engine::EngineState* state);
void DebugApplyGroundForces(Engine::EngineContext* ctx, Engine::EngineState* state);



#ifdef WDEBUG
void DebugRender(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
#endif
} // Ballance

#endif //WILL_ENGINE_BALLANCE_DEBUG_SYSTEM_H
