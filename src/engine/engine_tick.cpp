//
// Created by William on 2026-09-05.
//

#include "engine_tick.h"

#include <enkiTS/src/TaskScheduler.h>
#include <tracy/Tracy.hpp>

#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/material_manager.h"
#include "engine/components/fwd_components.h"
#include "engine/console/console.h"
#include "engine/editor/capture_shot_system.h"
#include "engine/editor/core_systems.h"
#include "engine/editor/ddgi_converge_boost.h"
#include "engine/editor/editor_systems.h"
#include "engine/editor/probe_bake_system.h"
#include "engine/systems/camera_system.h"
#include "engine/systems/common_systems.h"
#include "engine/systems/physics_system.h"
#include "engine/systems/render_systems.h"
#include "engine/systems/scene_system.h"
#include "engine/ui/ui.h"
#include "physics/physics_system.h"
#include "render/interface/render_interface.h"

namespace Engine
{
void ConnectEngineObservers(entt::registry& registry)
{
    ConnectPhysicsObservers(registry);
    ConnectCommonObservers(registry);
    ConnectRenderObservers(registry);
}

void PreUpdate(EngineContext* ctx, EngineState* state)
{
    ZoneScoped;

#if WILL_EDITOR
    EditorUpdate(ctx, state);
    EditorTickInput(ctx, state);
#endif

    FunctionKeyUpdate(ctx, state);

    UpdateUIPointerState(ctx, state);

#ifdef WDEBUG
    Console::Update(ctx, state);
#endif

    PhysicsPreUpdate(ctx, state);
}

void PostUpdate(EngineContext* ctx, EngineState* state)
{
    ZoneScoped;

#if WILL_EDITOR
    if (state->inputContext == InputContext::Editor) {
        UpdatePhysicsEditor(ctx, state);
    }
    if (state->inputContext != InputContext::Gameplay) {
        UpdateEditorCamera(ctx, state);
    }
#else
    if (state->inputContext == InputContext::Editor && ctx->bGameLoaded) {
        PlayStart(ctx, state);
    }
#endif

#if WILL_EDITOR
    ModelHotReload(ctx, state);
    FontHotReload(ctx, state);
    TextureHotReload(ctx, state);
    CubemapHotReload(ctx, state);
#endif

    PlaybackCommands(ctx, state);

    StaticMeshPendingKickoff(ctx, state);
    ReflectionProbeBakeUpgrade(ctx, state);
    ReflectionProbePendingKickoff(ctx, state);
    StaticMeshPrimitivePendingKickoff(ctx, state);
    ProceduralMeshPendingKickoff(ctx, state);
    SplineMeshPendingKickoff(ctx, state);
    ModuleMeshPendingKickoff(ctx, state);
    TextFontPendingKickoff(ctx, state);
    Text3DGeneratePendingKickoff(ctx, state);
    PhysicsMeshPendingKickoff(ctx, state);

    StaticMeshLoadResolve(ctx, state);
    LightSurfaceResolve(ctx, state);
    ReflectionProbeLoadResolve(ctx, state);
    StaticMeshPrimitiveLoadResolve(ctx, state);
    ProceduralMeshLoadResolve(ctx, state);
    SplineMeshLoadResolve(ctx, state);
    ModuleMeshLoadResolve(ctx, state);
    Text3DLoadResolve(ctx, state);
    PhysicsMeshLoadResolve(ctx, state);
    PhysicsShapeCreationResolve(ctx, state);
    PhysicsBodyCreationResolve(ctx, state);

    MarkRenderTransformsDirty(ctx, state);
    if (state->inputContext != InputContext::Editor) {
        MarkPhysicsTransformsDirty(state);
    }

    state->registry.clear<Component::DirtyTransformTag>();
    ctx->physicsSystem->ClearCollisionEvents();
    ctx->physicsSystem->ClearActivationEvents();
    ctx->materialManager->ProcessRetirements();
}

void PrepareFrame(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;

    ProbeBakeTick(ctx, state, frameBuffer);
    DDGIConvergeBoostTick(state->ddgiConvergeBoost, state->lighting.ddgi);
    CaptureShotTick(ctx, state, frameBuffer);

    FunctionKeyRenderUpdate(ctx, state, frameBuffer);

    BuildViewFamily(ctx, state, frameBuffer->mainViewFamily);

#if WILL_EDITOR
    {
        frameBuffer->selectedStableId = 0;
        if (!state->editor.selectedEntities.IsEmpty()) {
            entt::entity selected = state->editor.selectedEntities.Front();
            if (auto* stable = state->registry.try_get<Component::StableIdComponent>(selected)) {
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
        BuildPortalViewFamily(state, frameBuffer->mainViewFamily);
    }

    ResolveWorldTransforms(ctx, state);
    RenderPrepareTransforms(ctx, state, frameBuffer);
    SyncLightSurfaces(ctx, state);
    ResolveSkyboxCubemaps(ctx, state);
    //
    {
        ZoneScopedN("ParallelGathers");
        enki::TaskSet gatherTask(5, [&](enki::TaskSetPartition range, uint32_t) {
            for (uint32_t i = range.start; i < range.end; ++i) {
                switch (i) {
                    case 0: GatherRenderables(ctx, state, frameBuffer); break;
                    case 1: GatherLights(ctx, state, frameBuffer); break;
                    case 2: GatherTextRenderables(ctx, state, frameBuffer); break;
                    case 3: GatherReflectionProbes(ctx, state, frameBuffer); break;
                    case 4: GatherLocalDDGIVolumes(ctx, state, frameBuffer); break;
                    default: break;
                }
            }
        });
        ctx->scheduler->AddTaskSetToPipe(&gatherTask);
        ctx->scheduler->WaitforTask(&gatherTask);
    }
    GatherUIRenderables(ctx, state, frameBuffer);
    state->debug.bVerifyStoresOnce = false;

#if WILL_EDITOR
    DrawEditorInterface(ctx, state, frameBuffer);
    GatherEditorSprites(ctx, state, frameBuffer);
    GatherLightDebugDraws(ctx, state, frameBuffer);
#endif

#ifdef WDEBUG
    DebugRenderPhysics(ctx, state, frameBuffer);
#endif
}

void ScrubFrame(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ProbeBakeScrubFrame(ctx, state, frameBuffer);
    CaptureShotScrubFrame(ctx, state, frameBuffer);
}

void EndFrame(EngineContext* ctx)
{
    ctx->gameplayArena.Get().Reset();
#if WILL_EDITOR
    ctx->editorArena.Get().Reset();
#endif
}
} // Engine
