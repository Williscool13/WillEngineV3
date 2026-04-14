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
void FunctionKeySystem(Engine::EngineContext* ctx, Engine::EngineState* state);
}

#endif //WILL_ENGINE_CORE_SYSTEMS_H
