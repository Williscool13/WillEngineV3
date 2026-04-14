//
// Created by William on 2026-02-08.
//

#ifndef WILL_ENGINE_CORE_SYSTEMS_H
#define WILL_ENGINE_CORE_SYSTEMS_H

#include "engine/include/engine_context.h"

namespace Engine
{
struct EngineState;
}

namespace Core
{
struct FrameBuffer;
}

namespace Game
{
void FunctionKeyUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);
void FunctionKeyRenderUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
}

#endif //WILL_ENGINE_CORE_SYSTEMS_H
