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
#include "systems/render_systems.h"
#include "core/math/constants.h"

#include "fwd_components.h"
#include "components/common_components.h"
#include "engine/logging/engine_log.h"
#include "engine/logging/engine_logger.h"
#include "render/vulkan/vk_context.h"
#include "systems/debug_system.h"
#include "systems/camera_system.h"
#include "systems/editor_systems.h"
#include "systems/physics_system.h"
#include "gameplay/player/physics_player_controller.h"
#include "systems/common_systems.h"
#include "systems/gameplay_systems.h"


extern "C"
{
GAME_API void GameStartup(Core::EngineContext* ctx, Engine::GameState* state)
{
    SPDLOG_TRACE("Game Start Up");

    const entt::entity editorCamera = state->registry.create();
    state->registry.emplace<Game::Component::FreeCameraComponent>(editorCamera);
    state->registry.emplace<Game::Component::CameraComponent>(editorCamera);
    state->registry.emplace<Game::Component::EditorCameraTag>(editorCamera);
    Game::Component::TransformComponent& editorCameraTransform = state->registry.emplace<Game::Component::TransformComponent>(editorCamera);
    editorCameraTransform.translation = glm::vec3(0.0f, 3.0f, 5.0f);
    editorCameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 3.0f, 5.0f)), WORLD_UP);

    const entt::entity gameCamera = state->registry.create();
    state->registry.emplace<Game::Component::FreeCameraComponent>(gameCamera);
    state->registry.emplace<Game::Component::CameraComponent>(gameCamera);
    state->registry.emplace<Game::Component::GameCameraTag>(gameCamera);
    Game::Component::TransformComponent& gameCameraTransform = state->registry.emplace<Game::Component::TransformComponent>(gameCamera);
    gameCameraTransform.translation = glm::vec3(0.0f, 3.0f, 5.0f);
    gameCameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 3.0f, 5.0f)), WORLD_UP);


    state->registry.ctx().emplace<Engine::GameState*>(state);
    state->registry.ctx().emplace<Core::EngineContext*>(ctx);
}

GAME_API void GameLoad(Core::EngineContext* ctx, Engine::GameState* state)
{
#ifndef GAME_STATIC
    ctx->engineLogger->RegisterLoggersForDLL(Engine::LogCategory::Game);
#endif
    ImGui::SetCurrentContext(ctx->imguiContext);
    Physics::PhysicsSystem::RegisterPhysics();
    Audio::AudioManager::RegisterAudio();
    ctx->scheduler->RegisterExternalTaskThread();
    RegisterComponents(state->componentRegistry);
    Game::ConnectPhysicsObservers(state->registry);
    Game::ConnectCommonObservers(state->registry);
    Game::ConnectRenderObservers(state->registry);

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif

    LOG_INFO(Game, "Testing game reload4");
}

GAME_API void GameUpdate(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    const auto frameStart = std::chrono::high_resolution_clock::now();

#if WILL_EDITOR
    Game::EditorUpdate(ctx, state);
#endif

    if (state->bIsPlaying) {
        Game::DebugProcessPhysicsCollisions(ctx, state);
        Game::DebugApplyGroundForces(ctx, state);

        if (state->bEnablePhysics) {
            Game::UpdatePhysics(ctx, state);
        }

        if (auto* playerController = state->registry.ctx().find<Game::PhysicsPlayerController>()) {
            playerController->Update(ctx, state);
        }

        Game::UpdatePathMovers(ctx, state);
    }
    else {
        Game::UpdateEditorCamera(ctx, state);
    }

    Game::DebugUpdate(ctx, state);

    if (ctx->bModelLoadedThisFrame || state->bPendingModelResolve) {
        Game::ResolveStaticMeshLoads(ctx, state);
        Game::ResolveProceduralMeshLoads(ctx, state);
        Game::ResolveSplineMeshLoads(ctx, state);

        Game::ResolvePhysicsMeshLoads(ctx, state);
        state->bPendingModelResolve = false;
    }

    Game::ResolvePhysicsShapeCreation(ctx, state);
    Game::ResolvePhysicsBodyCreation(ctx, state);

    Game::RenderUpdate(ctx, state);
    state->registry.clear<Game::Component::DirtyTransformTag>();

    ctx->materialManager->ProcessRetirements();

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
    for (Core::CustomShaderDraw& draw : frameBuffer->mainViewFamily.customShaderDraws | std::views::values) {
        draw.instances.clear();
        draw.instanceBufferOffset = 0;
    }
    frameBuffer->mainViewFamily.materials.clear();
    frameBuffer->mainViewFamily.portalViews.clear();
#ifndef PACKAGED_BUILD
    frameBuffer->mainViewFamily.debugLines.clear();
    frameBuffer->mainViewFamily.debugBoxes.clear();
    frameBuffer->mainViewFamily.debugSpheres.clear();
#endif

    Game::BuildViewFamily(state, frameBuffer->mainViewFamily);
    if (state->bEnablePortal) {
        Game::BuildPortalViewFamily(state, frameBuffer->mainViewFamily);
    }

    Game::RenderPrepareTransforms(ctx, state, frameBuffer);
    Game::GatherRenderables(ctx, state, frameBuffer);

#if WILL_EDITOR
    Game::DrawEditorInterface(ctx, state, frameBuffer);
#endif

#ifndef PACKAGED_BUILD
    Game::DebugRender(ctx, state, frameBuffer);
    Game::DebugRenderPhysics(ctx, state, frameBuffer);
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
