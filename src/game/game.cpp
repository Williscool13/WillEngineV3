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
    state->registry.emplace<Engine::CameraComponent>(camera);
    state->registry.emplace<Engine::TransformComponent>(camera);
    state->registry.emplace<Engine::MainViewportComponent>(camera);

    spdlog::set_default_logger(ctx->logger);

}

GAME_API void GameLoad(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Load");

    spdlog::set_default_logger(ctx->logger);
    SPDLOG_TRACE("[Game] Registering engine component types:");
    SPDLOG_TRACE("  TransformComponent: {}",    entt::type_id<Engine::TransformComponent>().hash());
    SPDLOG_TRACE("  CameraComponent: {}",       entt::type_id<Engine::CameraComponent>().hash());
    SPDLOG_TRACE("  MainViewportComponent: {}", entt::type_id<Engine::MainViewportComponent>().hash());
    SPDLOG_TRACE("  DebugTestComponent: {}",    entt::type_id<Engine::DebugTestComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}",   entt::type_id<Game::FreeCameraComponent>().hash());
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    Game::System::UpdateCameras(ctx, state);

    Core::InputFrame gameInputCopy = *state->inputFrame;
    if (gameInputCopy.ConsumeKeyPress(Key::T)) {
        auto entity = state->registry.create();
        state->registry.emplace<Engine::DebugTestComponent>(entity, 0xDEADBEEF);
        SPDLOG_INFO("Game created DebugTest entity with value: 0x{:X}", 0xDEADBEEF);
    }

    if (gameInputCopy.ConsumeKeyPress(Key::Y)) {
        auto view = state->registry.view<Engine::DebugTestComponent>();
        SPDLOG_INFO("Engine found {} DebugTest entities", view.size());

        for (auto entity : view) {
            auto& test = view.get<Engine::DebugTestComponent>(entity);
            SPDLOG_INFO("Entity {} has value: 0x{:X}", static_cast<uint32_t>(entity), test.value);
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

GAME_API void GamePrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    // Game::BuildViewFamily(state, fb->mainViewFamily);

    frameBuffer->mainViewFamily.views.clear();
    auto cameraView = state->registry.view<Engine::CameraComponent, Engine::MainViewportComponent, Engine::TransformComponent>();

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
