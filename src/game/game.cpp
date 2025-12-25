//
// Created by William on 2025-12-14.
//

#include "spdlog/spdlog.h"

#include "core/include/game_interface.h"
#include "core/include/render_interface.h"
#include "core/input/input_frame.h"
#include "engine/engine_api.h"
#include "physics/physics_system.h"
#include "fwd_components.h"
#include "systems/debug_system.h"
#include "systems/camera_system.h"
#include "systems/physics_system.h"


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
    ctx->physicsSystem->RegisterAllocator();
    spdlog::set_default_logger(ctx->logger);
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    Game::System::UpdateCameras(ctx, state);
    Game::System::DebugUpdate(ctx, state);
    Game::System::UpdatePhysics(ctx, state);

    Core::InputFrame gameInputCopy = *state->inputFrame;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

GAME_API void GamePrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer, Render::FrameResources* frameResources)
{
    Game::System::BuildViewFamily(state, frameBuffer->mainViewFamily);
    Game::System::DebugPrepareFrame(ctx, state, frameBuffer, frameResources);
}


GAME_API void GameUnload(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Unload");
}

GAME_API void GameShutdown(Core::EngineContext* ctx, Engine::GameState* state)
{
    Game::System::DebugShutdown(ctx, state);
    SPDLOG_TRACE("Game Shutdown");
}
}
