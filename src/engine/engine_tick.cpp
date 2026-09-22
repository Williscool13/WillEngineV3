//
// Created by William on 2026-09-05.
//

#include "engine_tick.h"

#include <enkiTS/src/TaskScheduler.h>
#include <tracy/Tracy.hpp>

#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/material_manager.h"
#include "engine/components/component_types.h"
#include "engine/components/fwd_components.h"
#include "engine/components/render/light_components.h"
#include "engine/components/render/local_ddgi_volume_component.h"
#include "engine/components/render/reflection_probe_component.h"
#include "engine/components/render/text_component.h"
#include "engine/console/console.h"
#include "engine/editor/playtest_system.h"
#include "engine/editor/core_systems.h"
#include "engine/editor/ddgi_converge_boost.h"
#include "engine/editor/editor_systems.h"
#include "engine/editor/probe_bake_system.h"
#include "engine/systems/camera_system.h"
#include "engine/systems/common_systems.h"
#include "engine/systems/physics_system.h"
#include "engine/systems/render_systems.h"
#include "engine/systems/scene_system.h"
#include "engine/systems/system_graph.h"
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

void CollectPreUpdate(EngineContext* ctx, EngineState* state, SystemGraph& graph)
{
#if WILL_EDITOR
    graph.Add("EditorUpdate", &EditorUpdate);
    graph.Add("EditorTickInput", &EditorTickInput);
#endif

    graph.Add("FunctionKeyUpdate", &FunctionKeyUpdate);
    graph.Add("UpdateUIPointerState", &UpdateUIPointerState);

#ifdef WDEBUG
    graph.Add("ConsoleUpdate", &Console::Update);
#endif

    graph.Add("PhysicsPreUpdate", &PhysicsPreUpdate);
}

static void PostUpdateCleanup(EngineContext* ctx, EngineState* state)
{
    state->registry.clear<Component::DirtyTransformTag>();
    ctx->physicsSystem->ClearCollisionEvents();
    ctx->physicsSystem->ClearActivationEvents();
    ctx->materialManager->ProcessRetirements();
}

void CollectPostUpdate(EngineContext* ctx, EngineState* state, SystemGraph& graph)
{
#if WILL_EDITOR
    graph.Add("UpdatePhysicsEditor", [](EngineContext* ctx, EngineState* state) {
        if (state->inputContext == InputContext::Editor) { UpdatePhysicsEditor(ctx, state); }
    });
    graph.Add("UpdateEditorCamera", [](EngineContext* ctx, EngineState* state) {
        if (state->inputContext != InputContext::Gameplay) { UpdateEditorCamera(ctx, state); }
    });
#else
    graph.Add("PlayStart", [](EngineContext* ctx, EngineState* state) {
        if (state->inputContext == InputContext::Editor && ctx->bGameLoaded) { PlayStart(ctx, state); }
    });
#endif

#if WILL_EDITOR
    graph.Add("ModelHotReload", &ModelHotReload);
    graph.Add("FontHotReload", &FontHotReload);
    graph.Add("TextureHotReload", &TextureHotReload);
    graph.Add("CubemapHotReload", &CubemapHotReload);
#endif

    graph.Add("PlaybackCommands", &PlaybackCommands);

    graph.Add("StaticMeshPendingKickoff", &StaticMeshPendingKickoff);
    graph.Add("ReflectionProbeBakeUpgrade", &ReflectionProbeBakeUpgrade);
    graph.Add("ReflectionProbePendingKickoff", &ReflectionProbePendingKickoff);
    graph.Add("StaticMeshPrimitivePendingKickoff", &StaticMeshPrimitivePendingKickoff);
    graph.Add("ProceduralMeshPendingKickoff", &ProceduralMeshPendingKickoff);
    graph.Add("SplineMeshPendingKickoff", &SplineMeshPendingKickoff);
    graph.Add("ModuleMeshPendingKickoff", &ModuleMeshPendingKickoff);
    graph.Add("TextFontPendingKickoff", &TextFontPendingKickoff);
    graph.Add("Text3DGeneratePendingKickoff", &Text3DGeneratePendingKickoff);
    graph.Add("PhysicsMeshPendingKickoff", &PhysicsMeshPendingKickoff);

    graph.Add("StaticMeshLoadResolve", &StaticMeshLoadResolve);
    graph.Add("LightSurfaceResolve", &LightSurfaceResolve);
    graph.Add("ReflectionProbeLoadResolve", &ReflectionProbeLoadResolve);
    graph.Add("StaticMeshPrimitiveLoadResolve", &StaticMeshPrimitiveLoadResolve);
    graph.Add("ProceduralMeshLoadResolve", &ProceduralMeshLoadResolve);
    graph.Add("SplineMeshLoadResolve", &SplineMeshLoadResolve);
    graph.Add("ModuleMeshLoadResolve", &ModuleMeshLoadResolve);
    graph.Add("Text3DLoadResolve", &Text3DLoadResolve);
    graph.Add("PhysicsMeshLoadResolve", &PhysicsMeshLoadResolve);
    graph.Add("PhysicsShapeCreationResolve", &PhysicsShapeCreationResolve);
    graph.Add("PhysicsBodyCreationResolve", &PhysicsBodyCreationResolve);

    graph.Add("MarkRenderTransformsDirty", &MarkRenderTransformsDirty);
    graph.Add("MarkPhysicsTransformsDirty", [](EngineContext* ctx, EngineState* state) {
        if (state->inputContext != InputContext::Editor) { MarkPhysicsTransformsDirty(state); }
    });

    graph.Add("PostUpdateCleanup", &PostUpdateCleanup);
}

static void PublishFrameSettings(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
{
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
    frameBuffer->debug.bReGIRCursorCell = state->debug.diagnostics.bReGIRCursor;
    frameBuffer->debug.bWorldGridCursorCell = state->debug.diagnostics.bWorldGridCursor;
    frameBuffer->debug.pickRequestId = state->debug.pick.bPending ? state->debug.pick.requestId : 0u;
    frameBuffer->debug.pickU = state->debug.pick.u;
    frameBuffer->debug.pickV = state->debug.pick.v;
    if (!state->debug.bGIFreeze) {
        frameBuffer->debug.bFreezeGIField = false;
        frameBuffer->debug.bFreezeScreenFeedback = false;
        frameBuffer->debug.bFreezeGatherRay = false;
    }
    frameBuffer->restir = state->debug.restir;
    frameBuffer->ddgi = state->lighting.ddgi;
    frameBuffer->reflection = state->lighting.reflection;
    frameBuffer->reflectionProbe = state->lighting.reflectionProbe;
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
    frameBuffer->mainViewFamily.debugResourceName = state->debug.resourceName;
    frameBuffer->mainViewFamily.debugTransformationType = state->debug.transformationType;
    frameBuffer->mainViewFamily.debugViewAspect = state->debug.viewAspect;
    if (state->debug.bEnablePortal) {
        BuildPortalViewFamily(state, frameBuffer->mainViewFamily);
    }
}

void CollectPrepareFrame(EngineContext* ctx, EngineState* state, SystemGraph& graph)
{
    graph.Add("ProbeBakeTick", &ProbeBakeTick);
    graph.Add("DDGIConvergeBoost", [](EngineContext* ctx, EngineState* state) {
        DDGIConvergeBoostTick(state->ddgiConvergeBoost, state->lighting.ddgi);
    });
    graph.Add("PlaytestTick", &PlaytestTick);
    graph.Add("CameraRecordTick", &CameraRecordTick);

    graph.Add("FunctionKeyRenderUpdate", &FunctionKeyRenderUpdate);

    graph.Add("BuildViewFamily", [](EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer) {
        BuildViewFamily(ctx, state, frameBuffer->mainViewFamily);
    });
    graph.Add("PublishFrameSettings", &PublishFrameSettings);

    graph.Add("ResolveWorldTransforms", &ResolveWorldTransforms);
    graph.Add("RenderPrepareTransforms", &RenderPrepareTransforms);
    graph.Add("SyncLightSurfaces", &SyncLightSurfaces);
    graph.Add("ResolveSkyboxCubemaps", &ResolveSkyboxCubemaps);
    graph.Add("GatherRenderables", &GatherRenderables, {
        .bExclusive = false,
        .reads = {TypeSID<Component::SkyboxComponent>(), "assetManager"_sid, "materialManager"_sid, "engineConfig"_sid},
        .writes = {"instanceStore.dirty"_sid, "modelStore"_sid, "materialManager.uploadDirty"_sid, "viewFamily.renderables"_sid},
    });
    graph.Add("GatherLights", &GatherLights, {
        .bExclusive = false,
        .reads = {TypeSID<Component::MeshRuntime>(), TypeSID<Component::ProbeBakeHiddenTag>(), TypeSID<Component::DirectionalLightComponent>(), TypeSID<Component::TransformComponent>(), "instanceStore.visibility"_sid, "materialManager"_sid, "engineConfig"_sid},
        .writes = {"analyticLightStore"_sid, "triLightStore"_sid, "materialManager.changedDirty"_sid, "viewFamily.lights"_sid, "debug.emissive"_sid},
    });
    graph.Add("GatherTextRenderables", &GatherTextRenderables, {
        .bExclusive = false,
        .reads = {TypeSID<Component::TextComponent>(), TypeSID<Component::TextRuntime>(), TypeSID<Component::RenderTransformComponent>(), TypeSID<Component::TextFontPendingTag>(), TypeSID<Component::StableIdComponent>(), "assetManager"_sid, "materialManager"_sid},
        .writes = {"viewFamily.text"_sid},
    });
    graph.Add("GatherReflectionProbes", &GatherReflectionProbes, {
        .bExclusive = false,
        .reads = {TypeSID<Component::ReflectionProbeComponent>(), TypeSID<Component::WorldTransformComponent>(), "assetManager"_sid, "engineConfig"_sid},
        .writes = {"viewFamily.probes"_sid},
    });
    graph.Add("GatherLocalDDGIVolumes", &GatherLocalDDGIVolumes, {
        .bExclusive = false,
        .reads = {TypeSID<Component::LocalDDGIVolumeComponent>(), TypeSID<Component::WorldTransformComponent>(), "engineConfig"_sid},
        .writes = {"viewFamily.ddgiVolumes"_sid},
    });
    graph.Add("ClearVerifyStores", [](EngineContext* ctx, EngineState* state) {
        state->debug.bVerifyStoresOnce = false;
    });
    graph.Add("GatherUIRenderables", &GatherUIRenderables);

#if WILL_EDITOR
    graph.Add("DrawEditorInterface", &DrawEditorInterface);
    graph.Add("GatherEditorSprites", &GatherEditorSprites);
    graph.Add("GatherLightDebugDraws", &GatherLightDebugDraws);
#endif

#ifdef WDEBUG
    graph.Add("DebugRenderPhysics", &DebugRenderPhysics);
#endif
}

void ScrubFrame(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ProbeBakeScrubFrame(ctx, state, frameBuffer);
    PlaytestScrubFrame(ctx, state, frameBuffer);
}

void EndFrame(EngineContext* ctx)
{
    ctx->gameplayArena.Get().Reset();
#if WILL_EDITOR
    ctx->editorArena.Get().Reset();
#endif
}
} // Engine
