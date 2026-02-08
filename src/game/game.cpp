//
// Created by William on 2025-12-14.
//

#include <tracy/Tracy.hpp>

#include "spdlog/spdlog.h"

#include "core/include/game_interface.h"
#include "core/include/render_interface.h"
#include "core/input/input_frame.h"
#include "engine/engine_api.h"
#include "physics/physics_system.h"

#include "imgui.h"
#include "audio/audio_manager.h"
#include "systems/gather_renderables_system.h"
#include "core/math/constants.h"

#include "fwd_components.h"
#include "systems/debug_system.h"
#include "systems/camera_system.h"
#include "systems/editor_systems.h"
#include "systems/physics_system.h"


extern "C"
{
GAME_API void GameStartup(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Start Up");

    const entt::entity camera = state->registry.create();
    state->registry.emplace<Game::Component::FreeCameraComponent>(camera);
    state->registry.emplace<Game::Component::CameraComponent>(camera);
    Game::Component::TransformComponent& cameraTransform = state->registry.emplace<Game::Component::TransformComponent>(camera);
    cameraTransform.translation = glm::vec3(0.0f, 3.0f, 5.0f);
    cameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 3.0f, 5.0f)), WORLD_UP);
    state->registry.emplace<Game::Component::MainViewportComponent>(camera);
    state->registry.ctx().emplace<Engine::GameState*>(state);

    spdlog::set_default_logger(ctx->logger);
}

GAME_API void GameLoad(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("[Game] Registering engine component types:");
    SPDLOG_TRACE("  TransformComponent: {}", entt::type_id<Game::Component::TransformComponent>().hash());
    SPDLOG_TRACE("  CameraComponent: {}", entt::type_id<Game::Component::CameraComponent>().hash());
    SPDLOG_TRACE("  MainViewportComponent: {}", entt::type_id<Game::Component::MainViewportComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::Component::FreeCameraComponent>().hash());
    SPDLOG_TRACE("  PortalPlaneComponent: {}", entt::type_id<Game::Component::PortalPlaneComponent>().hash());
    SPDLOG_TRACE("  PortalComponent: {}", entt::type_id<Game::Component::PortalComponent>().hash());
    SPDLOG_TRACE("  AntiGravityComponent: {}", entt::type_id<Game::Component::AntiGravityComponent>().hash());
    SPDLOG_TRACE("  FloorComponent: {}", entt::type_id<Game::Component::FloorComponent>().hash());
    SPDLOG_TRACE("  DynamicPhysicsBodyComponent: {}", entt::type_id<Game::Component::DynamicPhysicsBodyComponent>().hash());
    SPDLOG_TRACE("  PhysicsBodyComponent: {}", entt::type_id<Game::Component::PhysicsBodyComponent>().hash());
    SPDLOG_TRACE("  DirtyPhysicsTransformComponent: {}", entt::type_id<Game::Component::DirtyPhysicsTransformComponent>().hash());
    SPDLOG_TRACE("  RenderableComponent: {}", entt::type_id<Game::Component::RenderableComponent>().hash());
    SPDLOG_TRACE("  TransformComponent: {}", entt::type_id<Game::Component::TransformComponent>().hash());

    spdlog::set_default_logger(ctx->logger);
    ImGui::SetCurrentContext(ctx->imguiContext);
    Physics::PhysicsSystem::RegisterPhysics();
    Audio::AudioManager::RegisterAudio();
    ctx->scheduler->RegisterExternalTaskThread();
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    const auto frameStart = std::chrono::high_resolution_clock::now();

    Game::System::UpdateCameras(ctx, state);
    Game::System::DebugUpdate(ctx, state);

    Game::System::DebugProcessPhysicsCollisions(ctx, state);
    Game::System::DebugApplyGroundForces(ctx, state);

    for (const auto& hotkey : Game::DEBUG_HOTKEYS) {
        if (state->inputFrame->GetKey(hotkey.key).pressed) {
            if (state->debugResourceName == hotkey.resourceName && state->debugViewAspect == hotkey.aspect) {
                state->debugResourceName.clear();
            }
            else {
                state->debugResourceName = hotkey.resourceName;
                state->debugTransformationType = hotkey.transform;
                state->debugViewAspect = hotkey.aspect;
            }
        }
    }

    if (state->bEnablePhysics) {
        Game::System::UpdatePhysics(ctx, state);
    }

    const auto frameEnd = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);
    constexpr auto targetFrameTime = std::chrono::microseconds(1000);

    if (elapsed < targetFrameTime) {
        ZoneScopedN("WaitForTargetFrameTime");
        std::this_thread::sleep_for(targetFrameTime - elapsed);
    }
}

GAME_API void GamePrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->mainViewFamily.modelMatrices.clear();
    frameBuffer->mainViewFamily.mainPassInstances.clear();
    for (Core::CustomStencilDrawBatch& customStencilBatch : frameBuffer->mainViewFamily.customStencilDraws) {
        customStencilBatch.instances.clear();
    }
    frameBuffer->mainViewFamily.materials.clear();
    frameBuffer->mainViewFamily.portalViews.clear();
#ifndef PACKAGED_BUILD
    frameBuffer->mainViewFamily.debugLines.clear();
    frameBuffer->mainViewFamily.debugBoxes.clear();
    frameBuffer->mainViewFamily.debugSpheres.clear();
#endif

    Game::System::BuildViewFamily(state, frameBuffer->mainViewFamily);
    if (state->bEnablePortal) {
        Game::System::BuildPortalViewFamily(state, frameBuffer->mainViewFamily);
    }

    Game::System::UpdateRenderTransforms(ctx, state, frameBuffer);
    Game::System::GatherRenderables(ctx, state, frameBuffer);


    Game::System::DrawEditorInterface(ctx, state, frameBuffer);

#ifndef PACKAGED_BUILD
    Game::System::DebugRender(ctx, state, frameBuffer);
    Game::System::DebugRenderPhysics(ctx, state, frameBuffer);
#endif
}

GAME_API void GameUnload(Core::EngineContext* ctx, Engine::GameState* state)
{
    if (ctx->scheduler) {
        ctx->scheduler->DeRegisterExternalTaskThread();
    }
}

GAME_API void GameShutdown(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Shutdown");
}
}
