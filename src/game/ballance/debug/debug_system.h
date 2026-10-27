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
class SystemRegistry;
}

namespace Core
{
struct FrameBuffer;
}

namespace Ballance
{
void DebugUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void DebugProcessPhysicsCollisions(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void DebugApplyGroundForces(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);



#ifdef WDEBUG
void DebugRender(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
#endif
} // Ballance

#endif //WILL_ENGINE_BALLANCE_DEBUG_SYSTEM_H
