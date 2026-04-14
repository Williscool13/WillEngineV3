//
// Created by William on 2025-12-14.
//

#include "game_interface.h"

#include "spdlog/include/spdlog/spdlog.h"

namespace Core
{

void StubStartup(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub Startup");
}

void StubLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub GameInit");
}

void StubUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{

}

void StubPrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, FrameBuffer* frameBuffer)
{

}

void StubUnload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub Unload");
}

void StubShutdown(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub GameShutdown");
}

} // Core
