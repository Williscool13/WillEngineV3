//
// Created by William on 2025-12-14.
//

#include "game_interface.h"

#include "spdlog/include/spdlog/spdlog.h"

namespace Core
{
size_t StubGetGameStateSize()
{
    return 0;
}

void StubStartup(EngineContext*, Game::GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub Startup");
}

void StubLoad(EngineContext*, Game::GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub GameInit");
}

void StubUpdate(EngineContext*, Game::GameState* state, InputFrame inputFrame, const TimeFrame* timeFrame)
{

}

void StubUnload(EngineContext*, Game::GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub Unload");
}

void StubShutdown(EngineContext*, Game::GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - stub GameShutdown");
}

} // Core
