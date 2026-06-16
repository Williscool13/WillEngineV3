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
#include "editor/editor_systems.h"
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
static void CreateCameras(Engine::EngineState* state, Vec3 editorPos, Quat editorRot)
{
    const entt::entity editorCamera = state->registry.create();
    state->registry.emplace<Game::Component::FreeCameraComponent>(editorCamera);
    state->registry.emplace<Game::Component::CameraComponent>(editorCamera);
    state->registry.emplace<Game::Component::EditorCameraTag>(editorCamera);
    auto& editorCameraTransform = state->registry.emplace<Game::Component::TransformComponent>(editorCamera);
    editorCameraTransform.translation = editorPos;
    editorCameraTransform.rotation = editorRot;

    const entt::entity gameCamera = state->registry.create();
    state->registry.emplace<Game::Component::FreeCameraComponent>(gameCamera);
    state->registry.emplace<Game::Component::CameraComponent>(gameCamera);
    state->registry.emplace<Game::Component::GameCameraTag>(gameCamera);
    auto& gameCameraTransform = state->registry.emplace<Game::Component::TransformComponent>(gameCamera);
    gameCameraTransform.translation = glm::vec3(0.0f, 3.0f, 5.0f);
    gameCameraTransform.rotation = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 3.0f, 5.0f)), WORLD_UP);
}

GAME_API void GameStartup(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Start Up");

    constexpr Vec3 defaultCameraPos{0.0f, 3.0f, 5.0f};
    const Quat defaultCameraRot = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - defaultCameraPos), WORLD_UP);
    CreateCameras(state, defaultCameraPos, defaultCameraRot);

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

    const Engine::FontID robotoId = ctx->assetManager->FindFontByName("Roboto");
    if (robotoId.IsValid()) {
        state->uiFont = ctx->assetManager->LoadFont(robotoId);
    }

    struct UIFontContext
    {
        Engine::AssetManager* assetManager;
        Engine::FontHandle handle;
    };
    static UIFontContext uiFontCtx{};
    uiFontCtx.assetManager = ctx->assetManager;
    uiFontCtx.handle = state->uiFont;

    Clay_SetMeasureTextFunction([](Clay_StringSlice text, Clay_TextElementConfig* config, void* userData) -> Clay_Dimensions {
        auto* fc = static_cast<UIFontContext*>(userData);
        const Engine::Font* font = fc->assetManager->GetFont(fc->handle);
        if (!font) { return {0.0f, static_cast<float>(config->fontSize)}; }
        const float scale = static_cast<float>(config->fontSize) / font->header.emSize;
        float width = 0.0f;
        for (int32_t i = 0; i < text.length; ++i) {
            const uint32_t cp = static_cast<unsigned char>(text.chars[i]);
            const Engine::WGlyphInfo* g = fc->assetManager->GetGlyph(fc->handle, cp);
            if (!g) {
                width += config->fontSize * 0.25f;
                continue;
            }
            width += g->advance * scale;
            if (i < text.length - 1) { width += config->letterSpacing; }
        }
        const float height = config->lineHeight > 0 ? static_cast<float>(config->lineHeight) : static_cast<float>(config->fontSize);
        return {width, height};
    }, &uiFontCtx);

    Audio::AudioManager::RegisterAudio();
    Game::RegisterComponents(state->componentRegistry);
    Game::ConnectPhysicsObservers(state->registry);
    Game::ConnectCommonObservers(state->registry);
    Game::ConnectRenderObservers(state->registry);

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif

    if (!state->projectConfig.defaultScene.IsEmpty()) {
        const auto& sceneCache = ctx->assetManager->GetSceneCache();
        for (const auto& pair : sceneCache) {
            if (pair.value.sceneName == state->projectConfig.defaultScene.c_str()) {
                auto res = Game::LoadSceneFromFile(state, ctx->assetManager, pair.key);
                if (res.bSuccess) {
                    state->currentSceneId = res.sceneId;
                    state->currentSceneName = res.sceneName;
                }
                break;
            }
        }
    }
}

GAME_API void GameHotReloadSave(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (state->bIsPlaying) {
        Game::PlayStop(ctx, state);
    }

    {
        auto camView = state->registry.view<Game::Component::EditorCameraTag, Game::Component::TransformComponent>();
        const auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = state->registry.get<Game::Component::TransformComponent>(camEntity);
            state->editor.pieCameraTranslation = transform.translation;
            state->editor.pieCameraRotation = transform.rotation;
        }
    }

    state->editor.hotReloadSnapshot = Game::SerializeAll(state->componentRegistry, state->registry, ctx->assetManager, state->editor.loadedScenes);
    Core::InlineVector<StringID, 8> scenesToUnload;
    for (Engine::RuntimeSceneMetadata scene : state->editor.loadedScenes) {
        scenesToUnload.PushBack(scene.sceneId);
    }
    Game::UnloadScenes(state, scenesToUnload);
    LOG_INFO(Game, "Hot reload: snapshot saved ({} scene(s))", state->editor.hotReloadSnapshot.Size());

    state->registry.clear();

    Game::DisconnectPhysicsObservers(state->registry);
    Game::DisconnectCommonObservers(state->registry);
    Game::DisconnectRenderObservers(state->registry);

#ifndef GAME_STATIC
    if (ctx->scheduler) {
        ctx->scheduler->DeRegisterExternalTaskThread();
    }
#endif
}

GAME_API void GameHotReloadLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
#ifndef GAME_STATIC
    ctx->engineLogger->RegisterLoggersForDLL(Engine::LogCategory::Game);

    ImGui::SetCurrentContext(ctx->imguiContext);
    ImGui::SetAllocatorFunctions(ctx->imguiAllocFn, ctx->imguiFreeFn, ctx->imguiAllocUserData);
    Clay_SetCurrentContext(ctx->clayContext);

    ctx->physicsSystem->RegisterPhysics();
    ctx->scheduler->RegisterExternalTaskThread();
#endif

    Game::RegisterComponents(state->componentRegistry);
    Game::ConnectPhysicsObservers(state->registry);
    Game::ConnectCommonObservers(state->registry);
    Game::ConnectRenderObservers(state->registry);

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif

    CreateCameras(state, state->editor.pieCameraTranslation, state->editor.pieCameraRotation);

    if (!state->editor.hotReloadSnapshot.IsEmpty()) {
        Game::DeserializeAll(state, state->editor.hotReloadSnapshot);
        state->editor.hotReloadSnapshot.Clear();
        LOG_INFO(Game, "Hot reload: snapshot restored");
    }
}
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
        Game::UpdateRotateInPlace(ctx, state);
        Game::CheckpointUpdate(ctx, state);
        Game::DeathZoneUpdate(ctx, state);

        if (auto* playerController = state->registry.ctx().find<Game::PhysicsPlayerController>()) {
            playerController->Update(ctx, state);
        }
    }
    else {
#if WILL_EDITOR
        Game::UpdateEditorCamera(ctx, state);
        Game::UpdatePhysicsEditor(ctx, state);
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
    if (state->bIsPlaying) {
        Game::MarkPhysicsTransformsDirty(state);
    }

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
    Game::FunctionKeyRenderUpdate(ctx, state, frameBuffer);

    Game::BuildViewFamily(state, frameBuffer->mainViewFamily);

#if WILL_EDITOR
    {
        frameBuffer->selectedStableId = 0;
        if (!state->editor.selectedEntities.IsEmpty()) {
            entt::entity selected = state->editor.selectedEntities.Front();
            if (auto* stable = state->registry.try_get<Game::Component::StableIdComponent>(selected)) {
                frameBuffer->selectedStableId = stable->id.id;
            }
        }
    }
#endif

    frameBuffer->bWireframe = state->debug.bWireframe;
    frameBuffer->bEnableShadeDispatchBucketingVisualization = state->debug.bEnableShadeDispatchBucketingVisualization;
    frameBuffer->bEnableLightingBucketingVisualization = state->debug.bEnableLightingBucketingVisualization;
    frameBuffer->restir = state->debug.restir;
    frameBuffer->mainViewFamily.lightingMode = state->lighting.lightingMode;
    frameBuffer->mainViewFamily.bResetGroundTruth = state->lighting.bResetGroundTruth;
    if (frameBuffer->mainViewFamily.lightingMode == Core::LightingMode::GroundTruthReSTIR) {
        const Core::RenderView& rv = frameBuffer->mainViewFamily.mainView;
        if (rv.currentViewData.view != rv.previousViewData.view) {
            frameBuffer->mainViewFamily.bResetGroundTruth = true;
        }
    }

    state->lighting.bResetGroundTruth = false;
    frameBuffer->mainViewFamily.shadingShaderOverride = state->debug.shadingShaderOverride;
    frameBuffer->mainViewFamily.lightingShaderOverride = state->debug.lightingShaderOverride;
    frameBuffer->mainViewFamily.postProcessConfig = state->lighting.postProcess;
    frameBuffer->mainViewFamily.gtaoConfig = state->lighting.gtaoConfig;
    frameBuffer->mainViewFamily.aaConfig = state->lighting.aaConfig;
    frameBuffer->mainViewFamily.sigmaParams = state->lighting.sigmaParams;
    if (state->debug.bEnablePortal) {
        Game::BuildPortalViewFamily(state, frameBuffer->mainViewFamily);
    }

    Game::RenderPrepareTransforms(ctx, state, frameBuffer);
    Game::GatherRenderables(ctx, state, frameBuffer);
    Game::GatherTextRenderables(ctx, state, frameBuffer);
    Game::GatherLights(ctx, state, frameBuffer);
    if (state->debug.bEnableUI) {
        Game::GatherUIRenderables(ctx, state, frameBuffer);
    }

#if WILL_EDITOR
    Game::DrawEditorInterface(ctx, state, frameBuffer);
    Game::GatherEditorSprites(ctx, state, frameBuffer);
    Game::GatherLightDebugDraws(ctx, state, frameBuffer);
#endif

#ifdef WDEBUG
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
