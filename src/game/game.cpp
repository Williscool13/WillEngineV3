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

GAME_API void GameUpdate(Core::EngineContext* ctx, Core::GameState* state, InputFrame inputFrame, const TimeFrame* timeFrame)
{
    if (inputFrame.GetKey(Key::F).pressed) {
        SPDLOG_INFO("Game Update on frame {}", timeFrame->frameCount);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

GAME_API void GameShutdown(Core::EngineContext* ctx, Core::GameState* state)
{
    SPDLOG_INFO("Game Shutdown");
}
}
