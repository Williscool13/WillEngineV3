//
// Created by William on 2025-12-14.
//

#include <tracy/Tracy.hpp>

#include "spdlog/spdlog.h"

#include "engine/include/game_interface.h"
#include "../render/interface/render_interface.h"
#include "core/input/input_frame.h"
#include "engine/engine_api.h"
#include "physics/physics_system.h"

#include "imgui.h"
#include "audio/audio_manager.h"
#include "systems/render_systems.h"
#include "core/math/constants.h"

#include "fwd_components.h"
#include "component-registry/component_registry.h"
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
#include "systems/core_systems.h"
#include "systems/gameplay_systems.h"


extern "C"
{
GAME_API void GameStartup(Engine::EngineContext* ctx, Engine::EngineState* state)
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


    state->registry.ctx().emplace<Engine::EngineState*>(state);
    state->registry.ctx().emplace<Engine::EngineContext*>(ctx);
}

GAME_API void GameLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
#ifndef GAME_STATIC
    ctx->engineLogger->RegisterLoggersForDLL(Engine::LogCategory::Game);

    ImGui::SetCurrentContext(ctx->imguiContext);
    ImGui::SetAllocatorFunctions(ctx->imguiAllocFn, ctx->imguiFreeFn, ctx->imguiAllocUserData);

    ctx->physicsSystem->RegisterPhysics();
#endif

    Audio::AudioManager::RegisterAudio();
    ctx->scheduler->RegisterExternalTaskThread();
    Game::RegisterComponents(state->componentRegistry);
    Game::ConnectPhysicsObservers(state->registry);
    Game::ConnectCommonObservers(state->registry);
    Game::ConnectRenderObservers(state->registry);

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif

    // if not editor, load the "default map", which needs to be stored in some engine config file
}

GAME_API void GameUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    const auto frameStart = std::chrono::high_resolution_clock::now();

#if WILL_EDITOR
    Game::EditorUpdate(ctx, state);
#endif

    Game::FunctionKeySystem(ctx, state);


    if (state->bIsPlaying) {
        if (state->bEnablePhysics) {
            Game::PhysicsUpdate(ctx, state);
        }
        Game::ResolveCollisionEvents(ctx, state);

        Game::DebugProcessPhysicsCollisions(ctx, state);
        Game::DebugApplyGroundForces(ctx, state);

        Game::UpdatePathMovers(ctx, state);
        Game::CheckpointUpdate(ctx, state);
        Game::DeathZoneUpdate(ctx, state);

        if (auto* playerController = state->registry.ctx().find<Game::PhysicsPlayerController>()) {
            playerController->Update(ctx, state);
        }
    }
    else {
#if WILL_EDITOR
        Game::UpdateEditorCamera(ctx, state);
#endif
    }

    Game::DebugUpdate(ctx, state);

    // Resolve Creations
    if (ctx->bModelLoadedThisFrame || state->bPendingModelResolve) {
        Game::ResolveStaticMeshLoads(ctx, state);
        Game::ResolveProceduralMeshLoads(ctx, state);
        Game::ResolveSplineMeshLoads(ctx, state);

        Game::ResolvePhysicsMeshLoads(ctx, state);
        state->bPendingModelResolve = false;
    }
    Game::ResolvePhysicsShapeCreation(ctx, state);
    Game::ResolvePhysicsBodyCreation(ctx, state);

    // Dirty carry-over to next frame
    Game::MarkRenderTransformsDirty(ctx, state);
    Game::MarkPhysicsTransformsDirty(state);

    // Frame Cleanup
    state->registry.clear<Game::Component::DirtyTransformTag>();
    ctx->physicsSystem->ClearCollisionEvents();
    ctx->physicsSystem->ClearActivationEvents();
    ctx->materialManager->ProcessRetirements();

    const auto frameEnd = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);
    constexpr auto targetFrameTime = std::chrono::microseconds(1000);

    if (elapsed < targetFrameTime) {
        ZoneScopedN("WaitForTargetFrameTime");
        std::this_thread::sleep_for(targetFrameTime - elapsed);
    }
}

GAME_API void GamePrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->mainViewFamily.modelMatrices.Clear();
    frameBuffer->mainViewFamily.mainPassInstances.Clear();
    for (const auto& pair : frameBuffer->mainViewFamily.customShaderDraws) {
        pair.value.instances.Clear();
        pair.value.instanceBufferOffset = 0;
    }
    frameBuffer->mainViewFamily.materials.Clear();
    frameBuffer->mainViewFamily.portalViews.Clear();
#ifndef PACKAGED_BUILD
    frameBuffer->mainViewFamily.debugLines.Clear();
    frameBuffer->mainViewFamily.debugBoxes.Clear();
    frameBuffer->mainViewFamily.debugSpheres.Clear();
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

GAME_API void GameUnload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (ctx->scheduler) {
        ctx->scheduler->DeRegisterExternalTaskThread();
    }
}

GAME_API void GameShutdown(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Shutdown");
}
}
