//
// Created by William on 2026-09-05.
//

#include "engine_tick.h"

#include <enkiTS/src/TaskScheduler.h>
#include <tracy/Tracy.hpp>

#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/material_manager.h"
#include "engine/components/component_types.h"
#include "engine/components/fwd_components.h"
#include "engine/components/core_components.h"
#include "engine/components/render_components.h"
#include "engine/components/camera_components.h"
#include "engine/components/common_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/components/physics/physics_components.h"
#include "engine/components/physics/physics_shape_runtime.h"
#include "engine/components/render/module_mesh_component.h"
#include "engine/components/render/post_process_volume_component.h"
#include "engine/components/render/procedural_mesh_component.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/components/render/static_mesh_primitive_component.h"
#include "engine/components/render/text3d_component.h"
#include "engine/components/motion/path_mover_component.h"
#include "engine/components/motion/rotate_in_place_component.h"
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
#include "engine/systems/motion_systems.h"
#include "engine/systems/physics_system.h"
#include "engine/systems/render_systems.h"
#include "engine/systems/scene_system.h"
#include "engine/systems/system_graph.h"
#include "engine/ui/ui.h"
#include "physics/physics_world.h"
#include "render/interface/render_interface.h"

namespace Engine
{
void ConnectEngineObservers(Engine::Registry& registry)
{
    ConnectPhysicsObservers(registry);
    ConnectCommonObservers(registry);
    ConnectRenderObservers(registry);
}

void CollectPreUpdate(EngineContext* ctx, EngineState* state, SystemGraph& graph)
{
#if WILL_EDITOR
    graph.Add("EditorUpdate", &EditorUpdate, AccessSet::EXCLUSIVE);
    graph.Add("EditorTickInput", &EditorTickInput, AccessSet::EXCLUSIVE);
#endif

    graph.Add("FunctionKeyUpdate", &FunctionKeyUpdate, AccessSet::EXCLUSIVE);
    graph.Add("UpdateUIPointerState", &UpdateUIPointerState, AccessSet::EXCLUSIVE);

#ifdef WDEBUG
    graph.Add("ConsoleUpdate", &Console::Update, AccessSet::EXCLUSIVE);
#endif

    graph.Add("PhysicsPreUpdate", &PhysicsPreUpdate, AccessSet::EXCLUSIVE);

    graph.Add("UpdatePathMovers", &UpdatePathMovers, SystemWhen::WorldRunning, Access<
        Reads<Component::PathMoverComponent>,
        Writes<Component::PathMoverRuntime, Component::TransformComponent, Component::DirtyTransformRequest>>());
    graph.Add("UpdateRotateInPlace", &UpdateRotateInPlace, SystemWhen::WorldRunning, Access<
        Reads<Component::RotateInPlaceComponent>,
        Writes<Component::TransformComponent, Component::DirtyTransformRequest>>());
}

static void TickQuietFrames(EngineContext* ctx, EngineState* state, SystemRegistry& registry)
{
    int32_t& quiet = state->assetLoad.quietFrames;
    if (CountLoadingEntities(state, registry) > 0 || ctx->assetManager->HasPendingLoads() || ctx->frameStatus.bAssetGenerationPending) {
        quiet = 0;
    }
    else if (quiet < INT32_MAX) {
        quiet++;
    }
}

static void PostUpdateCleanup(EngineContext* ctx, EngineState* state, SystemRegistry& registry)
{
    registry.Clear<Component::DirtyTransformRequest>();
    ctx->physicsWorld->ClearCollisionEvents();
    ctx->physicsWorld->ClearActivationEvents();
    ctx->materialManager->ProcessRetirements();
}

void CollectPostUpdate(EngineContext* ctx, EngineState* state, SystemGraph& graph)
{
    graph.Add("PropagateDirtyTransforms", &PropagateDirtyTransforms, AccessSet::EXCLUSIVE);

#if WILL_EDITOR
    graph.Add("UpdatePhysicsEditor", &UpdatePhysicsEditor, SystemWhen::Editing, Access<
        Reads<Component::PhysicsBodyRuntime, Component::TransformComponent, Component::DirtyTransformRequest, Component::HierarchyComponent>,
        Writes<Component::DynamicPhysicsBodyRuntime, Physics::PhysicsWorld>>());
    graph.Add("UpdateEditorCamera", [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) {
        if (state->inputContext != InputContext::Gameplay) { UpdateEditorCamera(ctx, state, registry); }
    }, AccessSet::EXCLUSIVE);
#else
    graph.Add("PlayStart", [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) {
        if (state->runMode == RunMode::Edit && ctx->bGameLoaded) { PlayStart(ctx, state, false, registry); }
    }, AccessSet::EXCLUSIVE);
#endif

#if WILL_EDITOR
    graph.Add("ModelHotReload", &ModelHotReload, AccessSet::EXCLUSIVE);
    graph.Add("FontHotReload", &FontHotReload, AccessSet::EXCLUSIVE);
    graph.Add("TextureHotReload", &TextureHotReload, AccessSet::EXCLUSIVE);
    graph.Add("CubemapHotReload", &CubemapHotReload, AccessSet::EXCLUSIVE);
#endif

    graph.Add("PlaybackCommands", &PlaybackCommands, AccessSet::EXCLUSIVE);

    graph.Add("StaticMeshPendingKickoff", &StaticMeshPendingKickoff, Access<
        Reads<Component::StaticMeshComponent>,
        Writes<Component::StaticMeshLoadRequest, Component::StaticMeshLoadingFlag, Component::MeshRuntime, AssetManager>>());
    graph.Add("ReflectionProbeBakeUpgrade", &ReflectionProbeBakeUpgrade, {
        .writes = {TypeSID<Component::ReflectionProbeComponent>(), TypeSID<Component::ReflectionProbeRuntime>(), TypeSID<Component::ReflectionProbeLoadingFlag>(), TypeSID<Component::ReflectionProbeLoadRequest>(), TypeSID<AssetManager>(), "state.rng"_sid},
    });
    graph.Add("ReflectionProbePendingKickoff", &ReflectionProbePendingKickoff, Access<
        Reads<Component::ReflectionProbeComponent>,
        Writes<Component::ReflectionProbeRuntime, Component::ReflectionProbeLoadRequest, Component::ReflectionProbeLoadingFlag, AssetManager>>());
    graph.Add("StaticMeshPrimitivePendingKickoff", &StaticMeshPrimitivePendingKickoff, Access<
        Reads<Component::StaticMeshPrimitiveComponent>,
        Writes<Component::StaticMeshPrimitiveLoadRequest, Component::StaticMeshPrimitiveLoadingFlag, Component::MeshRuntime, AssetManager>>());
    graph.Add("ProceduralMeshPendingKickoff", &ProceduralMeshPendingKickoff, Access<
        Reads<Component::ProceduralMeshComponent>,
        Writes<Component::ProceduralMeshLoadRequest, Component::ProceduralMeshLoadingFlag, Component::MeshRuntime, AssetManager>>());
    graph.Add("SplineMeshPendingKickoff", &SplineMeshPendingKickoff, Access<
        Reads<Component::SplineMeshComponent>,
        Writes<Component::SplineMeshLoadRequest, Component::SplineMeshLoadingFlag, Component::MeshRuntime, AssetManager>>());
    graph.Add("ModuleMeshPendingKickoff", &ModuleMeshPendingKickoff, Access<
        Reads<Component::ModuleMeshComponent>,
        Writes<Component::ModuleMeshLoadRequest, Component::ModuleMeshLoadingFlag, Component::MeshRuntime, AssetManager>>());
    graph.Add("TextFontPendingKickoff", &TextFontPendingKickoff, Access<
        Reads<Component::TextComponent>,
        Writes<Component::TextRuntime, Component::TextFontRequest, AssetManager>>());
    graph.Add("Text3DGeneratePendingKickoff", &Text3DGeneratePendingKickoff, Access<
        Reads<Component::Text3DComponent>,
        Writes<Component::Text3DGenerateRequest, Component::Text3DLoadingFlag, Component::MeshRuntime, AssetManager>>());
    graph.Add("PhysicsMeshPendingKickoff", &PhysicsMeshPendingKickoff, Access<
        Reads<Component::PhysicsBodyDesc, Component::PhysicsMotionOverrideRuntime>,
        Writes<Component::PhysicsShapeRuntime, Component::PhysicsMeshRequest, Component::PhysicsMeshLoadingFlag, AssetManager>>());

    graph.Add("StaticMeshLoadResolve", &StaticMeshLoadResolve, {
        .reads = {TypeSID<Component::StaticMeshComponent>(), TypeSID<Component::StaticMeshOverridesComponent>(), TypeSID<Component::RenderFlagsComponent>(), TypeSID<Component::NameComponent>(), TypeSID<Component::ProbeBakeHiddenFlag>(), TypeSID<Component::TransformComponent>(), TypeSID<Component::DynamicPhysicsBodyRuntime>(), TypeSID<Component::PhysicsBodyDesc>()},
        .writes = {TypeSID<Component::MeshRuntime>(), TypeSID<Component::StaticMeshLoadingFlag>(), TypeSID<Component::MultiframeDirtyRequest>(), TypeSID<AssetManager>(), TypeSID<MaterialManager>(), "modelStore"_sid, "instanceStore"_sid, "instanceStore.dirty"_sid, "instanceStore.boundsChanges"_sid, "instanceStore.visibility"_sid, "triLightStore"_sid, "state.assetLoad.pendingModelWait"_sid},
    });
    graph.Add("LightSurfaceResolve", &LightSurfaceResolve, {
        .reads = {TypeSID<Component::AreaLightComponent>(), TypeSID<Component::SphereLightComponent>(), TypeSID<Component::AreaLightRuntime>(), TypeSID<Component::SphereLightRuntime>(), TypeSID<Component::TransformComponent>(), TypeSID<Component::HierarchyComponent>()},
        .writes = {TypeSID<Component::LightSurfaceRequest>(), TypeSID<Component::LightSurfaceRuntime>(), TypeSID<AssetManager>(), TypeSID<MaterialManager>(), "state.builtinAssets"_sid, "modelStore"_sid, "instanceStore"_sid, "instanceStore.dirty"_sid, "instanceStore.boundsChanges"_sid, "instanceStore.visibility"_sid, "triLightStore"_sid},
    });
    graph.Add("ReflectionProbeLoadResolve", &ReflectionProbeLoadResolve, Access<
        Reads<Component::ReflectionProbeRuntime>,
        Writes<Component::ReflectionProbeLoadingFlag, AssetManager>>());
    graph.Add("StaticMeshPrimitiveLoadResolve", &StaticMeshPrimitiveLoadResolve, {
        .reads = {TypeSID<Component::StaticMeshPrimitiveComponent>(), TypeSID<Component::RenderFlagsComponent>(), TypeSID<Component::NameComponent>(), TypeSID<Component::ProbeBakeHiddenFlag>(), TypeSID<Component::TransformComponent>(), TypeSID<Component::DynamicPhysicsBodyRuntime>(), TypeSID<Component::PhysicsBodyDesc>()},
        .writes = {TypeSID<Component::MeshRuntime>(), TypeSID<Component::StaticMeshPrimitiveLoadingFlag>(), TypeSID<Component::MultiframeDirtyRequest>(), TypeSID<AssetManager>(), TypeSID<MaterialManager>(), "modelStore"_sid, "instanceStore"_sid, "instanceStore.dirty"_sid, "instanceStore.boundsChanges"_sid, "instanceStore.visibility"_sid, "triLightStore"_sid},
    });
    graph.Add("ProceduralMeshLoadResolve", &ProceduralMeshLoadResolve, {
        .reads = {TypeSID<Component::ProceduralMeshComponent>(), TypeSID<Component::RenderFlagsComponent>(), TypeSID<Component::NameComponent>(), TypeSID<Component::ProbeBakeHiddenFlag>(), TypeSID<Component::TransformComponent>(), TypeSID<Component::DynamicPhysicsBodyRuntime>(), TypeSID<Component::PhysicsBodyDesc>()},
        .writes = {TypeSID<Component::MeshRuntime>(), TypeSID<Component::ProceduralMeshLoadingFlag>(), TypeSID<Component::MultiframeDirtyRequest>(), TypeSID<AssetManager>(), TypeSID<MaterialManager>(), "modelStore"_sid, "instanceStore"_sid, "instanceStore.dirty"_sid, "instanceStore.boundsChanges"_sid, "instanceStore.visibility"_sid, "triLightStore"_sid, "state.assetLoad.pendingProceduralWait"_sid},
    });
    graph.Add("SplineMeshLoadResolve", &SplineMeshLoadResolve, {
        .reads = {TypeSID<Component::SplineMeshComponent>(), TypeSID<Component::RenderFlagsComponent>(), TypeSID<Component::NameComponent>(), TypeSID<Component::ProbeBakeHiddenFlag>(), TypeSID<Component::TransformComponent>(), TypeSID<Component::DynamicPhysicsBodyRuntime>(), TypeSID<Component::PhysicsBodyDesc>()},
        .writes = {TypeSID<Component::MeshRuntime>(), TypeSID<Component::SplineMeshLoadingFlag>(), TypeSID<Component::MultiframeDirtyRequest>(), TypeSID<AssetManager>(), TypeSID<MaterialManager>(), "modelStore"_sid, "instanceStore"_sid, "instanceStore.dirty"_sid, "instanceStore.boundsChanges"_sid, "instanceStore.visibility"_sid, "triLightStore"_sid},
    });
    graph.Add("ModuleMeshLoadResolve", &ModuleMeshLoadResolve, {
        .reads = {TypeSID<Component::ModuleMeshComponent>(), TypeSID<Component::RenderFlagsComponent>(), TypeSID<Component::ProbeBakeHiddenFlag>(), TypeSID<Component::TransformComponent>(), TypeSID<Component::DynamicPhysicsBodyRuntime>(), TypeSID<Component::PhysicsBodyDesc>()},
        .writes = {TypeSID<Component::MeshRuntime>(), TypeSID<Component::ModuleMeshLoadingFlag>(), TypeSID<Component::MultiframeDirtyRequest>(), TypeSID<AssetManager>(), TypeSID<MaterialManager>(), "modelStore"_sid, "instanceStore"_sid, "instanceStore.dirty"_sid, "instanceStore.boundsChanges"_sid, "instanceStore.visibility"_sid, "triLightStore"_sid},
    });
    graph.Add("Text3DLoadResolve", &Text3DLoadResolve, {
        .reads = {TypeSID<Component::Text3DComponent>(), TypeSID<Component::RenderFlagsComponent>(), TypeSID<Component::NameComponent>(), TypeSID<Component::ProbeBakeHiddenFlag>(), TypeSID<Component::TransformComponent>(), TypeSID<Component::DynamicPhysicsBodyRuntime>(), TypeSID<Component::PhysicsBodyDesc>()},
        .writes = {TypeSID<Component::MeshRuntime>(), TypeSID<Component::Text3DLoadingFlag>(), TypeSID<Component::MultiframeDirtyRequest>(), TypeSID<AssetManager>(), TypeSID<MaterialManager>(), "modelStore"_sid, "instanceStore"_sid, "instanceStore.dirty"_sid, "instanceStore.boundsChanges"_sid, "instanceStore.visibility"_sid, "triLightStore"_sid},
    });
    graph.Add("PhysicsMeshLoadResolve", &PhysicsMeshLoadResolve, Access<
        Reads<Component::PhysicsShapeRuntime>,
        Writes<Component::PhysicsMeshLoadingFlag, AssetManager>>());
    graph.Add("PhysicsShapeCreationResolve", &PhysicsShapeCreationResolve, Access<
        Reads<Component::PhysicsBodyDesc, Component::NameComponent, Component::PhysicsMeshRequest, Component::PhysicsMeshLoadingFlag>,
        Writes<Component::PhysicsShapeRuntime, Component::PhysicsShapeCreationRequest, AssetManager>>());
    graph.Add("PhysicsBodyCreationResolve", &PhysicsBodyCreationResolve, Access<
        Reads<Component::PhysicsBodyDesc, Component::PhysicsShapeRuntime, Component::PhysicsShapeCreationRequest, Component::TransformComponent, Component::HierarchyComponent, Component::PhysicsMotionOverrideRuntime>,
        Writes<Component::PhysicsBodyCreationRequest, Component::PhysicsBodyRuntime, Component::DynamicPhysicsBodyRuntime, Physics::PhysicsWorld>>());
    graph.Add("TickQuietFrames", &TickQuietFrames, AccessSet::EXCLUSIVE);
    graph.Add("RunLaunchExec", &Args::RunLaunchExec, AccessSet::EXCLUSIVE);

    graph.Add("MarkRenderTransformsDirty", &MarkRenderTransformsDirty, Access<
        Reads<Component::TransformComponent, Component::DirtyTransformRequest>,
        Writes<Component::MultiframeDirtyRequest>>());
    graph.Add("MarkPhysicsTransformsDirty", [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) { MarkPhysicsTransformsDirty(state, registry); }, SystemWhen::Running, {
        .reads = {TypeSID<Component::PhysicsBodyRuntime>(), TypeSID<Component::DirtyTransformRequest>(), TypeSID<Component::PhysicsBodyDesc>(), TypeSID<Component::PhysicsMotionOverrideRuntime>(), TypeSID<Component::HierarchyComponent>(), TypeSID<Component::NameComponent>(), "state.runMode"_sid},
        .writes = {TypeSID<Component::StaticFollowWarnedFlag>(), TypeSID<Component::DirtyKinematicPhysicsTransformRequest>(), TypeSID<Component::TeleportPhysicsTransformRequest>(), TypeSID<Component::SetVelocityRequest>(), "state.requests.bTeleportPhysics"_sid},
    });

    graph.Add("PostUpdateCleanup", &PostUpdateCleanup, AccessSet::EXCLUSIVE);
}

static void PublishFrameSettings(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer, SystemRegistry& registry)
{
#if WILL_EDITOR
    {
        frameBuffer->selectedStableId = 0;
        if (!state->editor.selectedEntities.IsEmpty()) {
            entt::entity selected = state->editor.selectedEntities.Front();
            if (auto* stable = registry.TryGet<Component::StableIdComponent>(selected)) {
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
    frameBuffer->renderReset = state->requests.renderReset;
    state->requests.renderReset = Core::RenderReset::None;
    frameBuffer->mainViewFamily.shadingShaderOverride = state->debug.shadingShaderOverride;
    frameBuffer->mainViewFamily.lightingShaderOverride = state->debug.lightingShaderOverride;
    frameBuffer->mainViewFamily.postProcessConfig = state->lighting.postProcess;
    frameBuffer->mainViewFamily.screenFade = state->screenFade;
    frameBuffer->mainViewFamily.gtaoConfig = state->lighting.gtaoConfig;
    frameBuffer->mainViewFamily.aaConfig = state->lighting.aaConfig;
    frameBuffer->mainViewFamily.sigmaParams = state->lighting.sigmaParams;
    frameBuffer->mainViewFamily.sunShadowMode = state->lighting.sunShadowMode;
    frameBuffer->mainViewFamily.csm = state->lighting.csm;
    frameBuffer->mainViewFamily.localShadows = state->lighting.localShadows;
    if (!state->debug.csm.bFreeze) {
        state->debug.csm.frozenAnchor = frameBuffer->mainViewFamily.mainView.currentViewData.cameraPos;
    }
    frameBuffer->mainViewFamily.csmAnchor = state->debug.csm.frozenAnchor;
    frameBuffer->mainViewFamily.iblIntensity = state->lighting.iblIntensity;
    frameBuffer->mainViewFamily.indirectIntensity = state->lighting.indirectIntensity;
    frameBuffer->mainViewFamily.resolutionScale = state->projectConfig.resolutionScale;
    frameBuffer->mainViewFamily.debugResourceName = state->debug.resourceName;
    frameBuffer->mainViewFamily.debugTransformationType = state->debug.transformationType;
    frameBuffer->mainViewFamily.debugViewAspect = state->debug.viewAspect;
}

void CollectPrepareFrame(EngineContext* ctx, EngineState* state, SystemGraph& graph)
{
    graph.Add("ProbeBakeTick", &ProbeBakeTick, AccessSet::EXCLUSIVE);
    graph.Add("ShadowBakeTick", &ShadowBakeTick, AccessSet::EXCLUSIVE);
    graph.Add("DDGIConvergeBoost", [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) {
        DDGIConvergeBoostTick(state->ddgiConvergeBoost, state->lighting.ddgi);
    }, {
        .writes = {"state.ddgiConvergeBoost"_sid, "engineConfig"_sid},
    });
    graph.Add("PlaytestTick", &PlaytestTick, AccessSet::EXCLUSIVE);
    graph.Add("CameraRecordTick", &CameraRecordTick, {
        .reads = {TypeSID<Component::EditorCameraTag>(), TypeSID<Component::TransformComponent>()},
        .writes = {"state.cameraRecorder"_sid},
    });

    graph.Add("FunctionKeyRenderUpdate", &FunctionKeyRenderUpdate, {
        .writes = {"state.requests.screenshot"_sid, "frameBuffer.screenshot"_sid},
    });

    graph.Add("BuildViewFamily", [](EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer, SystemRegistry& registry) {
        BuildViewFamily(ctx, state, frameBuffer->mainViewFamily, registry);
    }, {
        .reads = {TypeSID<Component::GameCameraTag>(), TypeSID<Component::EditorCameraTag>(), TypeSID<Component::TransformComponent>(), "state.inputContext"_sid, "state.requests.renderReset"_sid, "state.probeBake"_sid, "engineConfig"_sid},
        .writes = {TypeSID<Component::CameraRuntime>(), "state.renderedCamera"_sid, "viewFamily.mainView"_sid},
    });
    graph.Add("PublishFrameSettings", &PublishFrameSettings, AccessSet::EXCLUSIVE);

    graph.Add("ResolveWorldTransforms", &ResolveWorldTransforms, AccessSet::EXCLUSIVE);
    graph.Add("RenderPrepareTransforms", &RenderPrepareTransforms, {
        .reads = {TypeSID<Component::WorldTransformRuntime>(), TypeSID<Component::MeshRuntime>(), TypeSID<Component::AreaLightComponent>(), TypeSID<Component::SphereLightComponent>(), TypeSID<Component::LightSurfaceRuntime>(), TypeSID<Component::TextRuntime>(), TypeSID<Component::AreaLightRuntime>(), TypeSID<Component::SphereLightRuntime>(), TypeSID<Component::ProbeBakeHiddenFlag>(), "instanceStore.visibility"_sid},
        .writes = {TypeSID<Component::RenderTransformRuntime>(), TypeSID<Component::MultiframeDirtyRequest>(), "instanceStore.worldBounds"_sid, "instanceStore.boundsChanges"_sid, "modelStore"_sid, "triLightStore"_sid, "analyticLightStore"_sid},
    });
    graph.Add("SyncLightSurfaces", &SyncLightSurfaces, {
        .reads = {TypeSID<Component::AreaLightComponent>(), TypeSID<Component::SphereLightComponent>(), TypeSID<Component::ProbeBakeHiddenFlag>(), TypeSID<Component::ProbeBakeProxyHiddenFlag>(), "instanceStore.worldBounds"_sid},
        .writes = {TypeSID<Component::LightSurfaceRuntime>(), TypeSID<MaterialManager>(), "instanceStore.visibility"_sid, "instanceStore.dirty"_sid, "instanceStore.boundsChanges"_sid, "triLightStore"_sid},
    });
    graph.Add("ResolveSkyboxCubemaps", &ResolveSkyboxCubemaps, Access<
        Reads<Component::SkyboxComponent>,
        Writes<Component::SkyboxRuntime, AssetManager>>());
    graph.Add("ResolveBakedShadows", &ResolveBakedShadows, {
        .reads = {TypeSID<Component::AreaLightComponent>(), TypeSID<Component::SphereLightComponent>(), "analyticLightStore"_sid, "engineConfig"_sid},
        .writes = {TypeSID<Component::AreaLightRuntime>(), TypeSID<Component::SphereLightRuntime>(), TypeSID<AssetManager>(), "viewFamily.bakedShadowLights"_sid},
    });
    graph.Add("GatherRenderables", &GatherRenderables, {
        .reads = {TypeSID<Component::SkyboxComponent>(), TypeSID<Component::SkyboxRuntime>(), TypeSID<Component::VolumetricFogComponent>(), TypeSID<AssetManager>(), TypeSID<MaterialManager>(), "engineConfig"_sid, "instanceStore.visibility"_sid},
        .writes = {"instanceStore.dirty"_sid, "modelStore"_sid, "materialManager.uploadDirty"_sid, "triLightStore"_sid, "viewFamily.renderables"_sid, "viewFamily.volumetricFog"_sid},
    });
    graph.Add("GatherLights", &GatherLights, {
        .reads = {TypeSID<Component::MeshRuntime>(), TypeSID<Component::ProbeBakeHiddenFlag>(), TypeSID<Component::DirectionalLightComponent>(), TypeSID<Component::WorldTransformRuntime>(), TypeSID<Component::AreaLightComponent>(), TypeSID<Component::AreaLightRuntime>(), TypeSID<Component::SphereLightComponent>(), TypeSID<Component::SphereLightRuntime>(), "instanceStore.visibility"_sid, "instanceStore.worldBounds"_sid, "viewFamily.bakedShadowLights"_sid, "viewFamily.mainView"_sid, TypeSID<MaterialManager>(), "engineConfig"_sid},
        .writes = {"analyticLightStore"_sid, "triLightStore"_sid, "materialManager.changedDirty"_sid, "viewFamily.lights"_sid, "debug.emissive"_sid, "localShadowSelection"_sid, "instanceStore.boundsChanges"_sid},
    });
    graph.Add("GatherTextRenderables", &GatherTextRenderables, {
        .reads = {TypeSID<Component::TextComponent>(), TypeSID<Component::TextRuntime>(), TypeSID<Component::RenderTransformRuntime>(), TypeSID<Component::TextFontRequest>(), TypeSID<Component::StableIdComponent>(), TypeSID<AssetManager>(), TypeSID<MaterialManager>()},
        .writes = {"viewFamily.text"_sid},
    });
    graph.Add("GatherReflectionProbes", &GatherReflectionProbes, {
        .reads = {TypeSID<Component::ReflectionProbeComponent>(), TypeSID<Component::ReflectionProbeRuntime>(), TypeSID<Component::WorldTransformRuntime>(), TypeSID<AssetManager>(), "engineConfig"_sid},
        .writes = {"viewFamily.probes"_sid},
    });
    graph.Add("GatherLocalDDGIVolumes", &GatherLocalDDGIVolumes, {
        .reads = {TypeSID<Component::LocalDDGIVolumeComponent>(), TypeSID<Component::WorldTransformRuntime>(), "engineConfig"_sid},
        .writes = {"viewFamily.ddgiVolumes"_sid},
    });
    graph.Add("ApplyPostProcessVolumes", &ApplyPostProcessVolumes, {
        .reads = {TypeSID<Component::PostProcessVolumeComponent>(), TypeSID<Component::WorldTransformRuntime>(), "viewFamily.mainView"_sid},
        .writes = {"viewFamily.volumetricFog"_sid, "viewFamily.postProcess"_sid, "debug.postProcessVolumes"_sid},
    });
    graph.Add("GatherFogVolumes", &GatherFogVolumes, {
        .reads = {TypeSID<Component::FogVolumeComponent>(), TypeSID<Component::WorldTransformRuntime>(), "viewFamily.mainView"_sid},
        .writes = {"viewFamily.volumetricFog"_sid, "viewFamily.fogVolumes"_sid},
    });
    graph.Add("ClearVerifyStores", [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) {
        state->debug.bVerifyStoresOnce = false;
    }, {
        .writes = {"engineConfig"_sid},
    });
    graph.Add("GatherUIRenderables", &GatherUIRenderables, AccessSet::EXCLUSIVE);

#if WILL_EDITOR
    graph.Add("DrawEditorInterface", &DrawEditorInterface, AccessSet::EXCLUSIVE);
    graph.Add("GatherEditorSprites", &GatherEditorSprites, {
        .reads = {TypeSID<Component::AreaLightComponent>(), TypeSID<Component::SphereLightComponent>(), TypeSID<Component::DirectionalLightComponent>(), TypeSID<Component::ReflectionProbeComponent>(), TypeSID<Component::LocalDDGIVolumeComponent>(), TypeSID<Component::PostProcessVolumeComponent>(), TypeSID<Component::FogVolumeComponent>(), TypeSID<Component::WorldTransformRuntime>(), TypeSID<Component::StableIdComponent>(), "engineConfig"_sid},
        .writes = {"viewFamily.sprites"_sid},
    });
    graph.Add("GatherLightDebugDraws", &GatherLightDebugDraws, {
        .reads = {TypeSID<Component::AreaLightComponent>(), TypeSID<Component::SphereLightComponent>(), TypeSID<Component::DirectionalLightComponent>(), TypeSID<Component::LocalDDGIVolumeComponent>(), TypeSID<Component::PostProcessVolumeComponent>(), TypeSID<Component::FogVolumeComponent>(), TypeSID<Component::ReflectionProbeComponent>(), TypeSID<Component::ReflectionProbeRuntime>(), TypeSID<Component::WorldTransformRuntime>(), TypeSID<AssetManager>(), "engineConfig"_sid, "state.editor.selection"_sid},
        .writes = {"viewFamily.debugDraws"_sid},
    });
    graph.Add("GatherCSMDebugDraws", &GatherCSMDebugDraws, {
        .reads = {"engineConfig"_sid, "viewFamily.mainView"_sid, "viewFamily.lights"_sid},
        .writes = {"viewFamily.debugDraws"_sid},
    });
    graph.Add("GatherLocalShadowDebugDraws", &GatherLocalShadowDebugDraws, {
        .reads = {"engineConfig"_sid, "localShadowSelection"_sid},
        .writes = {"viewFamily.debugDraws"_sid},
    });
#endif

#ifdef WDEBUG
    graph.Add("DebugRenderPhysics", &DebugRenderPhysics, {
        .reads = {TypeSID<Component::MeshRuntime>(), TypeSID<Component::MultiframeDirtyRequest>(), TypeSID<Component::PhysicsBodyRuntime>(), TypeSID<Component::PhysicsBodyDesc>(), TypeSID<Component::DrawPhysicsDebugTag>(), "engineConfig"_sid, "state.editor.selection"_sid},
        .writes = {TypeSID<Physics::PhysicsWorld>(), "viewFamily.debugDraws"_sid},
    });
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
