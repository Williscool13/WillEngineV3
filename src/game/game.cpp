//
// Created by William on 2025-12-14.
//


#include "camera/free_camera.h"
#include "core/include/game_interface.h"
#include "spdlog/spdlog.h"


namespace Game
{
struct GameState
{
    FreeCamera camera;
    float value;
};
}


extern "C"
{
GAME_API size_t GameGetStateSize()
{
    return sizeof(Game::GameState);
}

GAME_API void GameStartup(Core::EngineContext* ctx, Game::GameState* state)
{
    spdlog::set_default_logger(ctx->logger);
    new(state) Game::GameState();

    state->camera = Game::FreeCamera({5.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    SPDLOG_DEBUG("Game Start Up");
}

GAME_API void GameLoad(Core::EngineContext* ctx, Game::GameState* state)
{
    spdlog::set_default_logger(ctx->logger);
    SPDLOG_DEBUG("Game Load");
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Game::GameState* state, InputFrame inputFrame, const TimeFrame* timeFrame)
{
    state->camera.Update(ctx, &inputFrame, timeFrame);
    if (inputFrame.GetKey(Key::F).pressed) {
        SPDLOG_DEBUG("Game Update on frame {}", timeFrame->frameCount);
    }

    glm::vec3 cameraPos = state->camera.GetPosition();
    float aspect = ctx->windowContext.windowWidth / static_cast<float>(ctx->windowContext.windowHeight);
    ctx->updateCamera(cameraPos, cameraPos + state->camera.GetForward(), state->camera.GetUp(),
                      state->camera.GetFov(), aspect, state->camera.GetNearPlane(), state->camera.GetFarPlane());

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}


GAME_API void GameUnload(Core::EngineContext* ctx, Game::GameState* state)
{
    SPDLOG_DEBUG("Game Unload");
}

GAME_API void GameShutdown(Core::EngineContext* ctx, Game::GameState* state)
{
    SPDLOG_DEBUG("Game Shutdown");
}
}
