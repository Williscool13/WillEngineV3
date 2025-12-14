//
// Created by William on 2025-12-14.
//


#include "core/include/game_interface.h"
#include "spdlog/spdlog.h"

extern "C"
{
GAME_API void GameInit(Core::EngineContext* ctx, Core::GameState* state)
{
    spdlog::set_default_logger(ctx->logger);
    SPDLOG_INFO("Game initialized");
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Core::GameState* state, float dt)
{
    SPDLOG_INFO("Game Update");
}

GAME_API void GameShutdown(Core::EngineContext* ctx, Core::GameState* state)
{
    SPDLOG_INFO("Game Shutdown");
}
}
