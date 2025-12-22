//
// Created by William on 2025-12-14.
//

#include "spdlog/spdlog.h"

#include "fwd_components.h"
#include "systems/camera_system.h"
#include "engine/engine_api.h"
#include "core/include/game_interface.h"
#include "core/include/render_interface.h"
#include "core/input/input_frame.h"
#include "render/types/render_types.h"
#include "systems/debug_system.h"


extern "C"
{
GAME_API void GameStartup(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Start Up");

    const entt::entity camera = state->registry.create();
    state->registry.emplace<Game::FreeCameraComponent>(camera);
    state->registry.emplace<Game::CameraComponent>(camera);
    state->registry.emplace<Game::TransformComponent>(camera);
    state->registry.emplace<Game::MainViewportComponent>(camera);

    spdlog::set_default_logger(ctx->logger);
}

GAME_API void GameLoad(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Load");

    spdlog::set_default_logger(ctx->logger);
    SPDLOG_TRACE("[Game] Registering engine component types:");
    SPDLOG_TRACE("  TransformComponent: {}", entt::type_id<Game::TransformComponent>().hash());
    SPDLOG_TRACE("  CameraComponent: {}", entt::type_id<Game::CameraComponent>().hash());
    SPDLOG_TRACE("  MainViewportComponent: {}", entt::type_id<Game::MainViewportComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::FreeCameraComponent>().hash());
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    Game::System::UpdateCameras(ctx, state);

    Game::System::DebugUpdate(ctx, state);

    Core::InputFrame gameInputCopy = *state->inputFrame;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

GAME_API void GamePrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    Game::System::BuildViewFamily(state, frameBuffer->mainViewFamily);
}


GAME_API void GameUnload(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Unload");
}

GAME_API void GameShutdown(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Shutdown");
}
}
