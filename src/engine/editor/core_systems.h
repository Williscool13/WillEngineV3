//
// Created by William on 2026-02-08.
//

#ifndef WILL_ENGINE_CORE_SYSTEMS_H
#define WILL_ENGINE_CORE_SYSTEMS_H

#include "engine/include/engine_context.h"

namespace Engine
{
struct EngineState;
class SystemRegistry;
}

namespace Core
{
struct FrameBuffer;
}

namespace Engine
{
void FunctionKeyUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void FunctionKeyRenderUpdate(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Core::FrameBuffer* frameBuffer);
}

#endif //WILL_ENGINE_CORE_SYSTEMS_H
