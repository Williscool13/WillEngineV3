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
#include "imgui.h"
#include "audio/audio_manager.h"
#include "components/gameplay/anti_gravity_component.h"
#include "components/gameplay/floor_component.h"
#include "components/gameplay/portals/portal_component.h"
#include "components/physics/physics_components.h"
#include "systems/gather_renderables_system.h"
#include "components/render/portal_plane_component.h"
#include "core/math/constants.h"
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
    state->registry.emplace<Game::FreeCameraComponent>(camera);
    state->registry.emplace<Game::CameraComponent>(camera);
    Game::TransformComponent& cameraTransform = state->registry.emplace<Game::TransformComponent>(camera);
    cameraTransform.translation = glm::vec3(0.0f, 3.0f, 5.0f);
    cameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 3.0f, 5.0f)), WORLD_UP);
    state->registry.emplace<Game::MainViewportComponent>(camera);
    state->registry.ctx().emplace<Engine::GameState*>(state);

    spdlog::set_default_logger(ctx->logger);
}

GAME_API void GameLoad(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("[Game] Registering engine component types:");
    SPDLOG_TRACE("  TransformComponent: {}", entt::type_id<Game::TransformComponent>().hash());
    SPDLOG_TRACE("  CameraComponent: {}", entt::type_id<Game::CameraComponent>().hash());
    SPDLOG_TRACE("  MainViewportComponent: {}", entt::type_id<Game::MainViewportComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::FreeCameraComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::PortalPlaneComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::PortalComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::AntiGravityComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::FloorComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::Component::DynamicPhysicsBodyComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::Component::PhysicsBodyComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::Component::DirtyPhysicsTransformComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::RenderableComponent>().hash());
    SPDLOG_TRACE("  FreeCameraComponent: {}", entt::type_id<Game::TransformComponent>().hash());

    spdlog::set_default_logger(ctx->logger);
    ImGui::SetCurrentContext(ctx->imguiContext);

    ctx->physicsSystem->RegisterAllocator();
    ctx->audioManager->RegisterAudio();
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
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

    Core::InputFrame gameInputCopy = *state->inputFrame;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

GAME_API void GamePrepareFrame(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->mainViewFamily.modelMatrices.clear();
    frameBuffer->mainViewFamily.mainInstances.clear();
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
    Game::System::GatherRenderables(ctx, state, frameBuffer);


    Game::System::DrawEditorInterface(ctx, state, frameBuffer);

    Game::System::DebugRender(ctx, state, frameBuffer);
    Game::System::DebugRenderPhysics(ctx, state, frameBuffer);
}

GAME_API void GameUnload(Core::EngineContext* ctx, Engine::GameState* state)
{
}

GAME_API void GameShutdown(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Shutdown");
}
}
