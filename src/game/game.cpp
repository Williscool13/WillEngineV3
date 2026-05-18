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
#include "engine/asset_manager.h"
#include "systems/scene_system.h"
#include "clay/clay.h"


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
    Clay_SetCurrentContext(ctx->clayContext);

    ctx->physicsSystem->RegisterPhysics();
    ctx->scheduler->RegisterExternalTaskThread();
#endif

    Audio::AudioManager::RegisterAudio();
    Game::RegisterComponents(state->componentRegistry);
    Game::ConnectPhysicsObservers(state->registry);
    Game::ConnectCommonObservers(state->registry);
    Game::ConnectRenderObservers(state->registry);

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif

#ifndef WILL_EDITOR
    if (!state->projectConfig.defaultScene.IsEmpty()) {
        const auto& sceneCache = ctx->assetManager->GetSceneCache();
        for (const auto& pair : sceneCache) {
            if (pair.value.sceneName == state->projectConfig.defaultScene.c_str()) {
                Game::LoadSceneFromFile(state, ctx->assetManager, pair.key);
                break;
            }
        }
    }
#endif
}

GAME_API void GameUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    const auto frameStart = std::chrono::high_resolution_clock::now();

#if WILL_EDITOR
    Game::EditorUpdate(ctx, state);
#endif

    Game::FunctionKeyUpdate(ctx, state);

    if (state->bIsPlaying) {
        if (state->physics.bEnabled) {
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
#else
        Game::PlayStart(ctx, state);
#endif
    }

    Game::DebugUpdate(ctx, state);

    // Resolve Creations
#if WILL_EDITOR
    Game::ResolveModelHotReloads(ctx, state);
    Game::ResolveFontHotReloads(ctx, state);
    Game::ResolveTextureHotReloads(ctx, state);
#endif
    Game::ResolveTextLoads(ctx, state);

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

static void GatherUIRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    Clay_SetLayoutDimensions({static_cast<float>(ctx->windowContext.viewportWidth), static_cast<float>(ctx->windowContext.viewportHeight)});

    const Vec2 mousePos = state->inputFrame->mousePositionAbsolute;
    const bool bIsMouseDown = state->inputFrame->GetMouse(MouseButton::LMB).down;
    Clay_SetPointerState(Clay_Vector2{mousePos.x, mousePos.y}, bIsMouseDown);

    const Vec2 mouseWheelDelta = state->inputFrame->mouseWheelDelta;
    Clay_UpdateScrollContainers(true, Clay_Vector2{mouseWheelDelta.x, mouseWheelDelta.y}, state->timeFrame->deltaTime);

    Clay_BeginLayout();

    constexpr Clay_Color COLOR_LIGHT = Clay_Color{224, 215, 210, 255};
    constexpr Clay_Color COLOR_RED = Clay_Color{168, 66, 28, 255};
    constexpr Clay_Color COLOR_ORANGE = Clay_Color{225, 138, 50, 255};

    uint32_t smilingFriendImageIndex = SMILING_FRIENDS_BINDLESS_INDEX;

    CLAY(CLAY_ID("OuterContainer"), { .layout = { .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 }, .backgroundColor = {250, 250, 255, 255} }) {
        CLAY(CLAY_ID("SideBar"), {
             .layout = {.sizing = {.width = CLAY_SIZING_FIXED(300), .height = CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16, .layoutDirection = CLAY_TOP_TO_BOTTOM, },
             .backgroundColor = COLOR_LIGHT
             }) {
            CLAY(CLAY_ID("ProfilePictureOuter"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = COLOR_RED }) {
                CLAY(CLAY_ID("ProfilePicture"), { .layout = { .sizing = { .width = CLAY_SIZING_FIXED(60), .height = CLAY_SIZING_FIXED(60) }}, .image = { .imageData = &smilingFriendImageIndex } }) {}
                CLAY_TEXT(CLAY_STRING("Clay - UI Library"), { .textColor = {255, 255, 255, 255}, .fontSize = 24, });
            }
            CLAY_TEXT(CLAY_STRING("WillEngine"), {.textColor = {255, 255, 255, 255}, .fontSize = 24, });

            CLAY(CLAY_ID("MainContent"), { .layout = { .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) } }, .backgroundColor = COLOR_LIGHT }) {}
        }
    }

    Clay_RenderCommandArray renderCommands = Clay_EndLayout(frameBuffer->timeFrame.deltaTime);
    (void) renderCommands;
}

GAME_API void GamePrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    frameBuffer->mainViewFamily.modelMatrices.Clear();
    frameBuffer->mainViewFamily.instances.Clear();
    frameBuffer->mainViewFamily.worldGlyphQuads.Clear();
    frameBuffer->mainViewFamily.textInstances.Clear();
    frameBuffer->mainViewFamily.activeTextMaterials.Clear();
    frameBuffer->mainViewFamily.textMaterials.Clear();
    frameBuffer->mainViewFamily.textDrawCalls.Clear();
    frameBuffer->mainViewFamily.activeMaterials.Clear();
    frameBuffer->mainViewFamily.materials.Clear();
    frameBuffer->mainViewFamily.lightingBuckets.Clear();
    frameBuffer->mainViewFamily.portalViews.Clear();
#ifndef PACKAGED_BUILD
    frameBuffer->mainViewFamily.debugLines.Clear();
    frameBuffer->mainViewFamily.debugBoxes.Clear();
    frameBuffer->mainViewFamily.debugSpheres.Clear();
#endif

    Game::FunctionKeyRenderUpdate(ctx, state, frameBuffer);

    Game::BuildViewFamily(state, frameBuffer->mainViewFamily);
    frameBuffer->bWireframe = state->debug.bWireframe;
    frameBuffer->bEnableShadeDispatchBucketingVisualization = state->debug.bEnableShadeDispatchBucketingVisualization;
    frameBuffer->bEnableLightingBucketingVisualization = state->debug.bEnableLightingBucketingVisualization;
    if (state->debug.bEnablePortal) {
        Game::BuildPortalViewFamily(state, frameBuffer->mainViewFamily);
    }

    Game::RenderPrepareTransforms(ctx, state, frameBuffer);
    Game::GatherRenderables(ctx, state, frameBuffer);
    Game::GatherTextRenderables(ctx, state, frameBuffer);
    GatherUIRenderables(ctx, state, frameBuffer);

#if WILL_EDITOR
    Game::DrawEditorInterface(ctx, state, frameBuffer);
#endif

#ifndef PACKAGED_BUILD
    Game::DebugRender(ctx, state, frameBuffer);
    Game::DebugRenderPhysics(ctx, state, frameBuffer);
#endif
}

GAME_API void GameEndFrame(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ctx->gameplayArena.Get().Reset();
#if WILL_EDITOR
    ctx->editorArena.Get().Reset();
#endif
}

GAME_API void GameUnload(Engine::EngineContext* ctx, Engine::EngineState* state)
{
#ifndef GAME_STATIC
    if (ctx->scheduler) {
        ctx->scheduler->DeRegisterExternalTaskThread();
    }
#endif
}

GAME_API void GameShutdown(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Shutdown");
}
}
