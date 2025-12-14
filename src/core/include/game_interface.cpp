//
// Created by William on 2025-12-14.
//

#include "game_interface.h"

#include "spdlog/include/spdlog/spdlog.h"

namespace Core
{
void StubInit(EngineContext*, GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - using stub GameInit");
}

void StubUpdate(EngineContext*, GameState* state, float deltaTime)
{
    SPDLOG_WARN("Game DLL not loaded - using stub GameUpdate");
}

void StubShutdown(EngineContext*, GameState* state)
{
    SPDLOG_WARN("Game DLL not loaded - using stub GameShutdown");
}
} // Core
