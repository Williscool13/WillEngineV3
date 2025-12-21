//
// Created by William on 2025-12-14.
//

#include "game_interface.h"

#include "spdlog/include/spdlog/spdlog.h"

namespace Core
{

void StubStartup(EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub Startup");
}

void StubLoad(EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub GameInit");
}

void StubUpdate(EngineContext* ctx, Engine::GameState* state)
{

}

void StubPrepareFrame(EngineContext* ctx, Engine::GameState* state, FrameBuffer* frameBuffer)
{

}

void StubUnload(EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub Unload");
}

void StubShutdown(EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub GameShutdown");
}

} // Core
