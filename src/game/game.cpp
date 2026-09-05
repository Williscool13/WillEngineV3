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
#include "engine/systems/render_systems.h"
#include "core/math/constants.h"

#include "fwd_components.h"
#include "engine/components/component_registration.h"
#include "components/component_registration.h"
#include "components/player_spawn_component.h"
#include "input/input_action_registry.h"
#include "engine/input_config.h"
#include "engine/components/common_components.h"
#include "engine/logging/engine_log.h"
#include "engine/logging/engine_logger.h"
#include "render/vulkan/vk_context.h"
#include "systems/debug_system.h"
#include "engine/systems/camera_system.h"
#include "engine/editor/capture_shot_system.h"
#include "engine/editor/probe_bake_system.h"
#include "engine/editor/ddgi_converge_boost.h"
#include "engine/editor/editor_systems.h"
#include "console/console.h"
#include "game_state.h"
#include "mcp/game_mcp_tools.h"
#include "logging/game_log_category.h"
#include "engine/systems/physics_system.h"
#include "gameplay/player/physics_player_controller.h"
#include "engine/systems/common_systems.h"
#include "engine/editor/core_systems.h"
#include "systems/gameplay_systems.h"
#include "engine/asset_manager.h"
#include "engine/systems/scene_system.h"
#include "ui/game_ui.h"
#include "clay/clay.h"
#include "engine/resources/font/font_metrics.h"

#ifndef GAME_STATIC
#include "meshoptimizer/src/meshoptimizer.h"
#include "par/par_shapes_ext.h"
#include "asset-load/asset-load-jobs/text3d_geometry.h"
#include "core/memory/concurrent_queue_traits.h"
#include "core/memory/memory_manager.h"

static Core::MemoryManager* gDllMemory = nullptr;

static void* DllMeshoptAlloc(size_t size)
{
    return gDllMemory->AssetsScratch().Alloc(size, Core::AllocTag::Meshopt);
}

static void DllMeshoptFree(void* ptr)
{
    gDllMemory->AssetsScratch().Free(ptr);
}

static void RegisterDllEngineHooks(Engine::EngineContext* ctx)
{
    if (ctx->engineLogger) {
        ctx->engineLogger->RegisterLoggersForDLL(Engine::LogCategory::Game);
    }

    ImGui::SetCurrentContext(ctx->imguiContext);
    ImGui::SetAllocatorFunctions(ctx->imguiAllocFn, ctx->imguiFreeFn, ctx->imguiAllocUserData);
    Clay_SetCurrentContext(ctx->clayContext);

    ctx->physicsSystem->RegisterPhysics();
    ctx->scheduler->RegisterExternalTaskThread();

    gDllMemory = ctx->memoryManager;
    meshopt_setAllocator(DllMeshoptAlloc, DllMeshoptFree);
    par_shapes_set_allocator(&ctx->memoryManager->AssetsScratch());
    AssetLoad::SetEarcutAllocator(&ctx->memoryManager->AssetsScratch());
    Core::SetConcurrentQueueAllocator(&ctx->memoryManager->General());
}
#endif

static void GamePlayStart(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    glm::vec3 spawnPosition{0.0f, 3.0f, 0.0f};
    int32_t bestPriority = INT32_MIN;
    auto spawnView = state->registry.view<Game::Component::PlayerSpawnComponent, Game::Component::TransformComponent>();
    for (auto entity : spawnView) {
        auto& spawn = spawnView.get<Game::Component::PlayerSpawnComponent>(entity);
        if (spawn.priority > bestPriority) {
            bestPriority = spawn.priority;
            spawnPosition = spawnView.get<Game::Component::TransformComponent>(entity).translation + spawn.offset;
        }
    }

    ctx->GetGameState<Game::GameState>()->playerController.Initialize(state, ctx, spawnPosition);
}

static void GamePlayStop(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Game::PhysicsPlayerController& playerController = ctx->GetGameState<Game::GameState>()->playerController;
    if (playerController.GetCharacter()) {
        playerController.Shutdown(ctx->physicsSystem);
    }
}

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

GAME_API size_t GameGetStateSize()
{
    return sizeof(Game::GameState);
}

GAME_API void GameStartup(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Start Up");

    new(ctx->gameState) Game::GameState();

    constexpr Vec3 defaultCameraPos{0.0f, 3.0f, 5.0f};
    const Quat defaultCameraRot = glm::quatLookAt(glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - defaultCameraPos), WORLD_UP);
    CreateCameras(state, defaultCameraPos, defaultCameraRot);

    state->registry.ctx().emplace<Engine::EngineState*>(state);
    state->registry.ctx().emplace<Engine::EngineContext*>(ctx);
}

GAME_API void GameLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
#ifndef GAME_STATIC
    RegisterDllEngineHooks(ctx);
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
        const float width = Engine::MeasureText(fc->assetManager, fc->handle, text.chars, text.length, config->fontSize, config->letterSpacing);
        const float height = config->lineHeight > 0 ? static_cast<float>(config->lineHeight) : static_cast<float>(config->fontSize);
        return {width, height};
    }, &uiFontCtx);

    Audio::AudioManager::RegisterAudio();
    Engine::RegisterEngineComponents(state->componentRegistry);
    Game::RegisterGameComponents(state->componentRegistry);
    Game::RegisterInputActions(state->input);
    Game::RegisterLogCategories(ctx->engineLogger);
    Game::Console::RegisterBuiltinCommands();
    Game::RegisterMCPTools(state);
    Engine::LoadAndApplyInputConfig(state->input, state->projectConfig);
    Engine::ConnectPhysicsObservers(state->registry);
    Engine::ConnectCommonObservers(state->registry);
    Game::ConnectGameplayObservers(state->registry);
    Engine::ConnectRenderObservers(state->registry);

    ctx->playStartFn = GamePlayStart;
    ctx->playStopFn = GamePlayStop;

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif

    const char* startupScene = !state->automation.sceneOverride.IsEmpty() ? state->automation.sceneOverride.c_str() : state->projectConfig.defaultScene.c_str();
    if (startupScene[0] != '\0') {
        bool bFound = false;
        const auto& sceneCache = ctx->assetManager->GetSceneCache();
        for (const auto& pair : sceneCache) {
            if (pair.value.sceneName == startupScene) {
                auto res = Engine::LoadSceneFromFile(state, ctx->assetManager, pair.key);
                if (res.bSuccess) {
                    state->scene.currentSceneId = res.sceneId;
                    state->scene.currentSceneName = res.sceneName;
                }
                bFound = true;
                break;
            }
        }
        if (!bFound && !state->automation.sceneOverride.IsEmpty()) {
            LOG_ERROR(Game, "--scene '{}' not found in the scene cache", startupScene);
        }
    }
}

GAME_API void GameHotReloadSave(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (Engine::IsPlaying(state)) {
        Engine::PlayStop(ctx, state);
    }
    //
    {
        auto camView = state->registry.view<Game::Component::EditorCameraTag, Game::Component::TransformComponent>();
        const auto camEntity = camView.front();
        if (camEntity != entt::null) {
            const auto& transform = state->registry.get<Game::Component::TransformComponent>(camEntity);
            state->editor.pieCameraTranslation = transform.translation;
            state->editor.pieCameraRotation = transform.rotation;
        }
    }

    state->editor.hotReloadSnapshot = Engine::SerializeAll(state->componentRegistry, state->registry, ctx->assetManager, state->editor.loadedScenes);
    Core::InlineVector<StringID, 8> scenesToUnload;
    for (Engine::RuntimeSceneMetadata scene : state->editor.loadedScenes) {
        scenesToUnload.PushBack(scene.sceneId);
    }
    Engine::UnloadScenes(state, scenesToUnload);
    LOG_INFO(Game, "Hot reload: snapshot saved ({} scene(s))", state->editor.hotReloadSnapshot.Size());

    state->registry.clear();

    Engine::DisconnectPhysicsObservers(state->registry);
    Engine::DisconnectCommonObservers(state->registry);
    Game::DisconnectGameplayObservers(state->registry);
    Engine::DisconnectRenderObservers(state->registry);

    ctx->playStartFn = {};
    ctx->playStopFn = {};

#ifndef GAME_STATIC
    if (ctx->scheduler) {
        ctx->scheduler->DeRegisterExternalTaskThread();
    }
    ctx->physicsSystem->UnregisterPhysics();
#endif
}

GAME_API void GameHotReloadLoad(Engine::EngineContext* ctx, Engine::EngineState* state)
{
#ifndef GAME_STATIC
    RegisterDllEngineHooks(ctx);
#endif

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
        const float width = Engine::MeasureText(fc->assetManager, fc->handle, text.chars, text.length, config->fontSize, config->letterSpacing);
        const float height = config->lineHeight > 0 ? static_cast<float>(config->lineHeight) : static_cast<float>(config->fontSize);
        return {width, height};
    }, &uiFontCtx);

    Engine::RegisterEngineComponents(state->componentRegistry);
    Game::RegisterGameComponents(state->componentRegistry);
    Game::RegisterInputActions(state->input);
    Game::RegisterLogCategories(ctx->engineLogger);
    Game::Console::RegisterBuiltinCommands();
    Game::RegisterMCPTools(state);
    Engine::LoadAndApplyInputConfig(state->input, state->projectConfig);
    Engine::ConnectPhysicsObservers(state->registry);
    Engine::ConnectCommonObservers(state->registry);
    Game::ConnectGameplayObservers(state->registry);
    Engine::ConnectRenderObservers(state->registry);

    ctx->playStartFn = GamePlayStart;
    ctx->playStopFn = GamePlayStop;

#if DEBUG
    gInternStringFn = ctx->internStringFn;
    gResolveStringIdFn = ctx->resolveStringIdFn;
#endif

    CreateCameras(state, state->editor.pieCameraTranslation, state->editor.pieCameraRotation);

    if (!state->editor.hotReloadSnapshot.IsEmpty()) {
        Engine::DeserializeAll(state, state->editor.hotReloadSnapshot);
        state->editor.hotReloadSnapshot.Clear();
        LOG_INFO(Game, "Hot reload: snapshot restored");
    }
}
}


GAME_API void GameUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;

    Game::TickQuietFrames(ctx, state);

#if WILL_EDITOR
    Engine::EditorUpdate(ctx, state);
    Engine::EditorTickInput(ctx, state);
#endif

    Engine::FunctionKeyUpdate(ctx, state);

    Engine::UpdateUIPointerState(ctx, state);

#ifdef WDEBUG
    Game::Console::Update(ctx, state);
#endif

    // Gameplay simulation runs only while playing AND game-focused
    if (state->inputContext == Engine::InputContext::Gameplay) {
        if (state->physics.bEnabled) {
            Engine::PhysicsUpdate(ctx, state);
        }
        Engine::ResolveCollisionEvents(ctx, state);

        Game::DebugProcessPhysicsCollisions(ctx, state);
        Game::DebugApplyGroundForces(ctx, state);

        Game::UpdatePathMovers(ctx, state);
        Game::UpdateRotateInPlace(ctx, state);
        Game::CheckpointUpdate(ctx, state);
        Game::DeathZoneUpdate(ctx, state);

        Game::PhysicsPlayerController& playerController = ctx->GetGameState<Game::GameState>()->playerController;
        if (playerController.GetCharacter()) {
            playerController.Update(ctx, state);
        }
    }
#if WILL_EDITOR
    if (state->inputContext == Engine::InputContext::Editor) {
        Engine::UpdatePhysicsEditor(ctx, state);
    }
    if (state->inputContext != Engine::InputContext::Gameplay) {
        Engine::UpdateEditorCamera(ctx, state);
    }
#else
    if (state->inputContext == Engine::InputContext::Editor) {
        Engine::PlayStart(ctx, state);
    }
#endif

    Game::DebugUpdate(ctx, state);

    // Resolve Creations
#if WILL_EDITOR
    Engine::ModelHotReload(ctx, state);
    Engine::FontHotReload(ctx, state);
    Engine::TextureHotReload(ctx, state);
    Engine::CubemapHotReload(ctx, state);
#endif

    Engine::StaticMeshPendingKickoff(ctx, state);
    Engine::ReflectionProbeBakeUpgrade(ctx, state);
    Engine::ReflectionProbePendingKickoff(ctx, state);
    Engine::StaticMeshPrimitivePendingKickoff(ctx, state);
    Engine::ProceduralMeshPendingKickoff(ctx, state);
    Engine::SplineMeshPendingKickoff(ctx, state);
    Engine::ModuleMeshPendingKickoff(ctx, state);
    Engine::TextFontPendingKickoff(ctx, state);
    Engine::Text3DGeneratePendingKickoff(ctx, state);
    Engine::PhysicsMeshPendingKickoff(ctx, state);

    state->assetLoad.bPendingModelResolve = false;
    Engine::StaticMeshLoadResolve(ctx, state);
    Engine::LightSurfaceResolve(ctx, state);
    Engine::ReflectionProbeLoadResolve(ctx, state);
    Engine::StaticMeshPrimitiveLoadResolve(ctx, state);
    Engine::ProceduralMeshLoadResolve(ctx, state);
    Engine::SplineMeshLoadResolve(ctx, state);
    Engine::ModuleMeshLoadResolve(ctx, state);
    Engine::Text3DLoadResolve(ctx, state);
    Engine::PhysicsMeshLoadResolve(ctx, state);
    Engine::PhysicsShapeCreationResolve(ctx, state);
    Engine::PhysicsBodyCreationResolve(ctx, state);

    // Dirty carry-over to next frame
    Engine::MarkRenderTransformsDirty(ctx, state);
    if (state->inputContext != Engine::InputContext::Editor) {
        Engine::MarkPhysicsTransformsDirty(state);
    }

    // Frame Cleanup
    state->registry.clear<Game::Component::DirtyTransformTag>();
    ctx->physicsSystem->ClearCollisionEvents();
    ctx->physicsSystem->ClearActivationEvents();
    ctx->materialManager->ProcessRetirements();
}

GAME_API void GamePrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    Engine::ProbeBakeTick(ctx, state, frameBuffer);
    Engine::DDGIConvergeBoostTick(state->ddgiConvergeBoost, state->lighting.ddgi);
    Engine::CaptureShotTick(ctx, state, frameBuffer);

    Engine::FunctionKeyRenderUpdate(ctx, state, frameBuffer);

    Engine::BuildViewFamily(ctx, state, frameBuffer->mainViewFamily);

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

    frameBuffer->debug = state->debug.render;
    if (!state->debug.bGIFreeze) {
        frameBuffer->debug.bFreezeGIField = false;
        frameBuffer->debug.bFreezeScreenFeedback = false;
        frameBuffer->debug.bFreezeGatherRay = false;
    }
    frameBuffer->restir = state->debug.restir;
    frameBuffer->ddgi = state->lighting.ddgi;
    frameBuffer->reflection = state->lighting.reflection;
    frameBuffer->reflectionProbe = state->lighting.reflectionProbe;
    state->debug.restir.bResetReGIR = false;
    frameBuffer->mainViewFamily.lightingMode = state->lighting.lightingMode;
    frameBuffer->mainViewFamily.groundTruthMode = state->lighting.groundTruthMode;
    frameBuffer->mainViewFamily.bResetGroundTruth = state->lighting.bResetGroundTruth;
    frameBuffer->mainViewFamily.groundTruthSpp = static_cast<uint32_t>(glm::max(1, state->lighting.groundTruthSpp));
    frameBuffer->mainViewFamily.groundTruthDofAperture = state->lighting.groundTruthDofAperture;
    if (frameBuffer->mainViewFamily.groundTruthMode != Core::GroundTruthMode::None) {
        const Core::RenderView& rv = frameBuffer->mainViewFamily.mainView;
        if (rv.currentViewData.view != rv.previousViewData.view) {
            frameBuffer->mainViewFamily.bResetGroundTruth = true;
        }
        static float prevGtAperture = -1.0f;
        static float prevGtFocus = -1.0f;
        const float gtFocus = state->lighting.postProcess.dofFocusDistance;
        if (state->lighting.groundTruthDofAperture != prevGtAperture || (state->lighting.groundTruthDofAperture > 0.0f && gtFocus != prevGtFocus)) {
            frameBuffer->mainViewFamily.bResetGroundTruth = true;
        }
        prevGtAperture = state->lighting.groundTruthDofAperture;
        prevGtFocus = gtFocus;
    }

    state->lighting.bResetGroundTruth = false;
    frameBuffer->cacheReset = state->requests.pendingCacheReset;
    state->requests.pendingCacheReset = Core::RenderCacheReset::None;
    frameBuffer->mainViewFamily.shadingShaderOverride = state->debug.shadingShaderOverride;
    frameBuffer->mainViewFamily.lightingShaderOverride = state->debug.lightingShaderOverride;
    frameBuffer->mainViewFamily.postProcessConfig = state->lighting.postProcess;
    frameBuffer->mainViewFamily.screenFade = state->screenFade;
    frameBuffer->mainViewFamily.gtaoConfig = state->lighting.gtaoConfig;
    frameBuffer->mainViewFamily.aaConfig = state->lighting.aaConfig;
    frameBuffer->mainViewFamily.sigmaParams = state->lighting.sigmaParams;
    frameBuffer->mainViewFamily.iblIntensity = state->lighting.iblIntensity;
    frameBuffer->mainViewFamily.indirectIntensity = state->lighting.indirectIntensity;
    frameBuffer->mainViewFamily.resolutionScale = state->projectConfig.resolutionScale;
    if (state->debug.bEnablePortal) {
        Engine::BuildPortalViewFamily(state, frameBuffer->mainViewFamily);
    }

    Engine::ResolveWorldTransforms(ctx, state);
    Engine::RenderPrepareTransforms(ctx, state, frameBuffer);
    Engine::GatherLights(ctx, state, frameBuffer);
    Engine::GatherReflectionProbes(ctx, state, frameBuffer);
    Engine::GatherLocalDDGIVolumes(ctx, state, frameBuffer);
    Engine::GatherRenderables(ctx, state, frameBuffer);
    Engine::GatherTextRenderables(ctx, state, frameBuffer);
    Game::GatherUIRenderables(ctx, state, frameBuffer);
    state->debug.bVerifyStoresOnce = false;

#if WILL_EDITOR
    Engine::DrawEditorInterface(ctx, state, frameBuffer);
    Engine::GatherEditorSprites(ctx, state, frameBuffer);
    Engine::GatherLightDebugDraws(ctx, state, frameBuffer);
#endif

#ifdef WDEBUG
    Game::DebugRender(ctx, state, frameBuffer);
    Engine::DebugRenderPhysics(ctx, state, frameBuffer);
#endif

    Engine::ProbeBakeScrubFrame(ctx, state, frameBuffer);
    Engine::CaptureShotScrubFrame(ctx, state, frameBuffer);
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
    ctx->playStartFn = {};
    ctx->playStopFn = {};

#ifndef GAME_STATIC
    if (ctx->scheduler) {
        ctx->scheduler->DeRegisterExternalTaskThread();
    }
    ctx->physicsSystem->UnregisterPhysics();
#endif
}

GAME_API void GameShutdown(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    SPDLOG_TRACE("Game Shutdown");

    ctx->GetGameState<Game::GameState>()->~GameState();
}
