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
    SPDLOG_TRACE("  TransformComponent: {}",    entt::type_id<TransformComponent>().hash());
    SPDLOG_TRACE("  CameraComponent: {}",       entt::type_id<CameraComponent>().hash());
    SPDLOG_TRACE("  MainViewportComponent: {}", entt::type_id<MainViewportComponent>().hash());
    SPDLOG_TRACE("  DebugTestComponent: {}",    entt::type_id<DebugTestComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}",   entt::type_id<Game::FreeCameraComponent>().hash());
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    Game::System::UpdateCameras(ctx, state);

    Core::InputFrame gameInputCopy = *state->inputFrame;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

GAME_API void GamePrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    // Game::BuildViewFamily(state, fb->mainViewFamily);

    frameBuffer->mainViewFamily.views.clear();
    auto cameraView = state->registry.view<Game::CameraComponent, Game::MainViewportComponent, Game::TransformComponent>();

    for (auto entity : cameraView) {
        const auto& [cam, transform] = cameraView.get(entity);

        Core::RenderView view;
        view.viewMatrix = cam.view;
        view.projectionMatrix = cam.projection;
        view.cameraPosition = glm::vec3(transform.transform.translation);
        view.frustum = Render::CreateFrustum(view.projectionMatrix);
        frameBuffer->mainViewFamily.views.push_back(view);
    }
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
