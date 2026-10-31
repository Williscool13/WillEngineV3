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
#include "engine/editor/mobility_assign.h"
#include "engine/editor/editor_systems.h"
#include "engine/editor/probe_bake_system.h"
#include "engine/systems/camera_system.h"
#include "engine/systems/common_systems.h"
#include "engine/systems/motion_systems.h"
#include "engine/systems/physics_system.h"
#include "engine/systems/render_systems.h"
#include "engine/systems/render_systems_helpers.h"
#include "engine/systems/scene_system.h"
#include "engine/systems/system_graph.h"
#include "engine/systems/transform_systems.h"
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
    graph.Add("EditorUpdate",
        &EditorUpdate,
        AccessSet::EXCLUSIVE);
    graph.Add("EditorTickInput",
        &EditorTickInput,
        AccessSet::EXCLUSIVE);
#endif

    graph.Add("FunctionKeyUpdate",
        &FunctionKeyUpdate,
        AccessSet::EXCLUSIVE);
    graph.Add("UpdateUIPointerState",
        &UpdateUIPointerState,
        AccessSet::EXCLUSIVE);

#ifdef WDEBUG
    graph.Add("ConsoleUpdate",
        &Console::Update,
        AccessSet::EXCLUSIVE);
#endif

    graph.Add("PhysicsPreUpdate",
        &PhysicsPreUpdate,
        AccessSet::EXCLUSIVE);

    graph.Add("UpdatePathMovers",
        &UpdatePathMovers,
        SystemWhen::WorldRunning,
        Access<
            Reads<Component::PathMoverComponent>,
            Writes<Component::PathMoverRuntime, Component::TransformComponent>,
            DeferredWrites<Component::DirtyTransformRequest>>());
    graph.Add("UpdateRotateInPlace",
        &UpdateRotateInPlace,
        SystemWhen::WorldRunning,
        Access<
            Reads<Component::RotateInPlaceComponent>,
            Writes<Component::TransformComponent>,
            DeferredWrites<Component::DirtyTransformRequest>>());
}

static void PostUpdateCleanup(EngineContext* ctx, EngineState* state, SystemRegistry& registry)
{
    for (const entt::entity entity : registry.View<Component::DirtyTransformRequest>()) {
        registry.Remove<Component::DirtyTransformRequest>(entity);
    }
    registry.ServiceMut<Physics::PhysicsWorld>().ClearCollisionEvents();
    registry.ServiceMut<Physics::PhysicsWorld>().ClearActivationEvents();
    registry.ServiceMut<Engine::MaterialManager>().ProcessRetirements();
}

void CollectPostUpdate(EngineContext* ctx, EngineState* state, SystemGraph& graph)
{
    // Sorted only when the order is dirty at collect time; an order dirtied later in the frame is sorted next frame
    if (state->bHierarchyOrderDirty) {
        graph.Add("SortHierarchy",
            &SortHierarchy,
            AccessSet::EXCLUSIVE);
    }
    graph.Add("RejectStaticTransformWrites",
        [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) { RejectStaticTransformWrites(state, registry); },
        SystemWhen::Running,
        Access<
            Reads<Component::NameComponent>,
            Writes<Component::StaticTransformLockRuntime, Component::TransformComponent, Component::DirtyTransformRequest>>());
    graph.Add("PropagateDirtyTransforms",
        &PropagateDirtyTransforms,
        Access<
            Reads<Component::HierarchyComponent, Component::DynamicPhysicsBodyRuntime>,
            DeferredWrites<Component::DirtyTransformRequest>>());

#if WILL_EDITOR
    graph.Add("UpdatePhysicsEditor",
        &UpdatePhysicsEditor,
        SystemWhen::Editing,
        Access<
            Reads<Component::PhysicsBodyRuntime, Component::TransformComponent, Component::DirtyTransformRequest, Component::HierarchyComponent>,
            Writes<Component::DynamicPhysicsBodyRuntime, Physics::PhysicsWorld>>());
    graph.Add("UpdateEditorCamera",
        [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) {
            if (state->inputContext != InputContext::Gameplay) { UpdateEditorCamera(ctx, state, registry); }
        },
        AccessSet::EXCLUSIVE);
#else
    graph.Add("PlayStart",
        [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) {
            if (state->runMode == RunMode::Edit && ctx->bGameLoaded) { PlayStart(ctx, state, registry, false); }
        },
        AccessSet::EXCLUSIVE);
#endif

#if WILL_EDITOR
    graph.Add("ModelHotReload",
        &ModelHotReload,
        AccessSet::EXCLUSIVE);
    graph.Add("FontHotReload",
        &FontHotReload,
        AccessSet::EXCLUSIVE);
    graph.Add("TextureHotReload",
        &TextureHotReload,
        AccessSet::EXCLUSIVE);
    graph.Add("CubemapHotReload",
        &CubemapHotReload,
        AccessSet::EXCLUSIVE);
#endif

    graph.Add("PlaybackCommands",
        &PlaybackCommands,
        AccessSet::EXCLUSIVE);

    graph.Add("ReflectionProbeBakeUpgrade",
        &ReflectionProbeBakeUpgrade,
        Access<
            Writes<Component::ReflectionProbeComponent, Component::ReflectionProbeRuntime, Component::ReflectionProbeLoadingFlag, Component::ReflectionProbeLoadRequest>>());
    graph.Add("StaticMeshPendingKickoff",
        &StaticMeshPendingKickoff,
        Access<
            Reads<Component::StaticMeshComponent, Component::StaticMeshLoadRequest>>());
    graph.Add("ReflectionProbePendingKickoff",
        &ReflectionProbePendingKickoff,
        Access<
            Reads<Component::ReflectionProbeComponent, Component::ReflectionProbeRuntime, Component::ReflectionProbeLoadRequest>>());
    graph.Add("StaticMeshPrimitivePendingKickoff",
        &StaticMeshPrimitivePendingKickoff,
        Access<
            Reads<Component::StaticMeshPrimitiveComponent, Component::StaticMeshPrimitiveLoadRequest>>());
    graph.Add("ProceduralMeshPendingKickoff",
        &ProceduralMeshPendingKickoff,
        Access<
            Reads<Component::ProceduralMeshComponent, Component::ProceduralMeshLoadRequest>>());
    graph.Add("SplineMeshPendingKickoff",
        &SplineMeshPendingKickoff,
        Access<
            Reads<Component::SplineMeshComponent, Component::SplineMeshLoadRequest>>());
    graph.Add("ModuleMeshPendingKickoff",
        &ModuleMeshPendingKickoff,
        Access<
            Reads<Component::ModuleMeshComponent, Component::ModuleMeshLoadRequest>>());
    graph.Add("TextFontPendingKickoff",
        &TextFontPendingKickoff,
        Access<
            Reads<Component::TextComponent, Component::TextRuntime>,
            Writes<Component::TextFontRequest>>());
    graph.Add("Text3DGeneratePendingKickoff",
        &Text3DGeneratePendingKickoff,
        Access<
            Reads<Component::Text3DComponent, Component::Text3DGenerateRequest>>());
    graph.Add("PhysicsMeshPendingKickoff",
        &PhysicsMeshPendingKickoff,
        Access<
            Reads<Component::PhysicsBodyDesc, Component::PhysicsShapeRuntime, Component::PhysicsMeshRequest, Component::PhysicsMotionOverrideRuntime>>());

    graph.Add("StaticMeshLoadResolve",
        &StaticMeshLoadResolve,
        Access<
            Reads<Component::StaticMeshComponent, Component::MeshRuntime>,
            DeferredWrites<Component::StaticMeshLoadingFlag>,
            After<&StaticMeshPendingKickoff>>());
    graph.Add("LightSurfaceResolve",
        &LightSurfaceResolve,
        Access<
            DeferredWrites<Component::LightSurfaceRequest>>());
    graph.Add("ReflectionProbeLoadResolve",
        &ReflectionProbeLoadResolve,
        Access<
            Reads<Component::ReflectionProbeRuntime>,
            DeferredWrites<Component::ReflectionProbeLoadingFlag>,
            After<&ReflectionProbePendingKickoff>>());
    graph.Add("StaticMeshPrimitiveLoadResolve",
        &StaticMeshPrimitiveLoadResolve,
        Access<
            Reads<Component::StaticMeshPrimitiveComponent, Component::MeshRuntime>,
            DeferredWrites<Component::StaticMeshPrimitiveLoadingFlag>,
            After<&StaticMeshPrimitivePendingKickoff>>());
    graph.Add("ProceduralMeshLoadResolve",
        &ProceduralMeshLoadResolve,
        Access<
            Reads<Component::ProceduralMeshComponent, Component::MeshRuntime>,
            DeferredWrites<Component::ProceduralMeshLoadingFlag>,
            After<&ProceduralMeshPendingKickoff>>());
    graph.Add("SplineMeshLoadResolve",
        &SplineMeshLoadResolve,
        Access<
            Reads<Component::SplineMeshComponent, Component::MeshRuntime>,
            DeferredWrites<Component::SplineMeshLoadingFlag>,
            After<&SplineMeshPendingKickoff>>());
    graph.Add("ModuleMeshLoadResolve",
        &ModuleMeshLoadResolve,
        Access<
            Reads<Component::ModuleMeshComponent, Component::MeshRuntime>,
            DeferredWrites<Component::ModuleMeshLoadingFlag>,
            After<&ModuleMeshPendingKickoff>>());
    graph.Add("Text3DLoadResolve",
        &Text3DLoadResolve,
        Access<
            Reads<Component::Text3DComponent, Component::MeshRuntime>,
            DeferredWrites<Component::Text3DLoadingFlag>,
            After<&Text3DGeneratePendingKickoff>>());
    graph.Add("PhysicsMeshLoadResolve",
        &PhysicsMeshLoadResolve,
        Access<
            Reads<Component::PhysicsShapeRuntime>,
            DeferredWrites<Component::PhysicsMeshLoadingFlag>,
            After<&PhysicsMeshPendingKickoff>>());
    graph.Add("PhysicsShapeCreationResolve",
        &PhysicsShapeCreationResolve,
        Access<
            Reads<Component::PhysicsBodyDesc, Component::NameComponent, Component::PhysicsMeshRequest, Component::PhysicsMeshLoadingFlag>,
            Writes<Component::PhysicsShapeRuntime, Component::PhysicsShapeCreationRequest>>());
    graph.Add("PhysicsBodyCreationResolve",
        &PhysicsBodyCreationResolve,
        Access<
            Reads<Component::PhysicsBodyDesc, Component::PhysicsShapeRuntime, Component::PhysicsShapeCreationRequest, Component::TransformComponent, Component::HierarchyComponent, Component::PhysicsMotionOverrideRuntime>,
            Writes<Component::PhysicsBodyCreationRequest, Component::PhysicsBodyRuntime, Component::DynamicPhysicsBodyRuntime, Physics::PhysicsWorld>>());
    graph.Add("RunLaunchExec",
        &Args::RunLaunchExec,
        AccessSet::EXCLUSIVE);

    graph.Add("MarkPhysicsTransformsDirty",
        [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) { MarkPhysicsTransformsDirty(state, registry); },
        SystemWhen::Running,
        Access<
            Reads<Component::PhysicsBodyRuntime, Component::DirtyTransformRequest, Component::PhysicsBodyDesc, Component::PhysicsMotionOverrideRuntime, Component::HierarchyComponent, Component::NameComponent>,
            Writes<Component::StaticFollowWarnedFlag, Component::DirtyKinematicPhysicsTransformRequest, Component::TeleportPhysicsTransformRequest, Component::SetVelocityRequest>>());

    graph.Add("ResolveWorldTransforms",
        &ResolveWorldTransforms,
        Access<
            Reads<Component::TransformComponent, Component::HierarchyComponent, Component::DynamicPhysicsBodyRuntime, Component::RenderWorldTransformRuntime, Component::DirtyTransformRequest>,
            DeferredWrites<Component::RenderTransformDirtyRequest>,
            Writes<Component::WorldTransformRuntime>>());

    graph.Add("PostUpdateCleanup",
        &PostUpdateCleanup,
        Access<
            DeferredWrites<Component::DirtyTransformRequest>,
            Writes<Physics::PhysicsWorld, MaterialManager>>());
}

static void PublishFrameSettings(EngineContext* ctx, EngineState* state, SystemRegistry& registry)
{
    Core::FrameBuffer& frameBuffer = registry.FrameMut<Core::FrameBuffer>();
#if WILL_EDITOR
    {
        frameBuffer.selectedStableId = 0;
        if (!state->editor.selectedEntities.IsEmpty()) {
            entt::entity selected = state->editor.selectedEntities.Front();
            if (auto* stable = registry.TryGet<Component::StableIdComponent>(selected)) {
                frameBuffer.selectedStableId = stable->id.id;
            }
        }
    }
#endif

    Core::ViewFamilySettings& settings = registry.FrameMut<Core::ViewFamilySettings>();
    const Core::RenderView& mainView = registry.Frame<Core::RenderView>();

    frameBuffer.debug = state->debug.render;
    frameBuffer.debug.bReGIRCursorCell = state->debug.diagnostics.bReGIRCursor;
    frameBuffer.debug.bWorldGridCursorCell = state->debug.diagnostics.bWorldGridCursor;
    frameBuffer.debug.pickRequestId = state->debug.pick.bPending ? state->debug.pick.requestId : 0u;
    frameBuffer.debug.pickU = state->debug.pick.u;
    frameBuffer.debug.pickV = state->debug.pick.v;
    if (!state->debug.bGIFreeze) {
        frameBuffer.debug.bFreezeGIField = false;
        frameBuffer.debug.bFreezeScreenFeedback = false;
    }
    frameBuffer.restir = state->debug.restir;
    frameBuffer.ddgi = state->lighting.ddgi;
    frameBuffer.reflection = state->lighting.reflection;
    frameBuffer.reflectionProbe = state->lighting.reflectionProbe;
    settings.lightingMode = state->lighting.lightingMode;
    settings.groundTruthMode = state->lighting.groundTruthMode;
    settings.bResetGroundTruth = state->lighting.bResetGroundTruth;
    settings.groundTruthSpp = static_cast<uint32_t>(glm::max(1, state->lighting.groundTruthSpp));
    settings.groundTruthDofAperture = state->lighting.groundTruthDofAperture;
    if (settings.groundTruthMode != Core::GroundTruthMode::None) {
        const Core::RenderView& rv = mainView;
        if (rv.currentViewData.view != rv.previousViewData.view) {
            settings.bResetGroundTruth = true;
        }
        static float prevGtAperture = -1.0f;
        static float prevGtFocus = -1.0f;
        const float gtFocus = state->lighting.postProcess.dofFocusDistance;
        if (state->lighting.groundTruthDofAperture != prevGtAperture || (state->lighting.groundTruthDofAperture > 0.0f && gtFocus != prevGtFocus)) {
            settings.bResetGroundTruth = true;
        }
        prevGtAperture = state->lighting.groundTruthDofAperture;
        prevGtFocus = gtFocus;
    }

    state->lighting.bResetGroundTruth = false;
    frameBuffer.renderReset = state->requests.renderReset;
    state->requests.renderReset = Core::RenderReset::None;
    settings.shadingShaderOverride = state->debug.shadingShaderOverride;
    settings.lightingShaderOverride = state->debug.lightingShaderOverride;
    settings.postProcessConfig = state->lighting.postProcess;
    settings.screenFade = state->screenFade;
    settings.gtaoConfig = state->lighting.gtaoConfig;
    settings.aaConfig = state->lighting.aaConfig;
    settings.sigmaParams = state->lighting.sigmaParams;
    settings.sunShadowMode = state->lighting.sunShadowMode;
    settings.csm = state->lighting.csm;
    settings.localShadows = state->lighting.localShadows;
    if (!state->debug.csm.bFreeze) {
        state->debug.csm.frozenAnchor = mainView.currentViewData.cameraPos;
    }
    settings.csmAnchor = state->debug.csm.frozenAnchor;
    settings.iblIntensity = state->lighting.iblIntensity;
    settings.indirectIntensity = state->lighting.indirectIntensity;
    settings.resolutionScale = state->projectConfig.resolutionScale;
    settings.debugResourceName = state->debug.resourceName;
    settings.debugTransformationType = state->debug.transformationType;
    settings.debugViewAspect = state->debug.viewAspect;
}

void CollectPrepareFrame(EngineContext* ctx, EngineState* state, SystemGraph& graph)
{
    graph.Add("ProbeBakeTick",
        &ProbeBakeTick,
        AccessSet::EXCLUSIVE);
    graph.Add("ShadowBakeTick",
        &ShadowBakeTick,
        AccessSet::EXCLUSIVE);
    graph.Add("DDGIConvergeBoost",
        [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) {
            DDGIConvergeBoostTick(state->ddgiConvergeBoost, state->lighting.ddgi);
        },
        Access<
            Writes<Core::DDGIParams>>());
    graph.Add("PlaytestTick",
        &PlaytestTick,
        AccessSet::EXCLUSIVE);
    graph.Add("CameraRecordTick",
        &CameraRecordTick,
        Access<
            Reads<Component::EditorCameraTag, Component::TransformComponent>>());

    graph.Add("FunctionKeyRenderUpdate",
        &FunctionKeyRenderUpdate,
        Access<
            Writes<Core::FrameBuffer>>());

    graph.Add("BuildViewFamily",
        &BuildViewFamily,
        Access<
            Reads<Component::GameCameraTag, Component::EditorCameraTag, Component::TransformComponent, FrameRequests>,
            Writes<Component::CameraRuntime, Core::RenderView>>());
    graph.Add("PublishFrameSettings",
        &PublishFrameSettings,
        Access<
            Reads<Component::StableIdComponent, Core::RenderView, Core::DDGIParams>,
            Writes<Core::FrameBuffer, Core::ViewFamilySettings, FrameRequests>>());

    graph.Add("ResolveRenderTransforms",
        &ResolveRenderTransforms,
        Access<
            Reads<Component::WorldTransformRuntime, Component::TransformComponent, Component::HierarchyComponent, Component::DynamicPhysicsBodyRuntime>,
            DeferredWrites<Component::RenderTransformDirtyRequest, Component::MultiframeDirtyRequest>,
            Writes<Component::RenderWorldTransformRuntime>>());
    graph.Add("RenderPrepareTransforms",
        &RenderPrepareTransforms,
        Access<
            Reads<Component::RenderWorldTransformRuntime, Component::MeshRuntime, Component::AreaLightComponent, Component::SphereLightComponent, Component::LightSurfaceRuntime, Component::TextRuntime, Component::AreaLightRuntime, Component::SphereLightRuntime, Component::ProbeBakeHiddenFlag, InstanceRecords>,
            Writes<Component::RenderTransformRuntime, Component::MultiframeDirtyRequest, InstanceWorldBounds, InstanceBoundsChanges, ModelStore, TriLightStore, AnalyticLightStore>>());
    graph.Add("SyncLightSurfaces",
        &SyncLightSurfaces,
        Access<
            Reads<Component::AreaLightComponent, Component::SphereLightComponent, Component::ProbeBakeHiddenFlag, Component::ProbeBakeProxyHiddenFlag>,
            Writes<Component::LightSurfaceRuntime, MaterialManager, InstanceStore, TriLightStore>>());
    graph.Add("ResolveSkyboxCubemaps",
        &ResolveSkyboxCubemaps,
        Access<
            Reads<Component::SkyboxComponent>,
            Writes<Component::SkyboxRuntime>>());
    graph.Add("ResolveBakedShadows",
        &ResolveBakedShadows,
        Access<
            Reads<Component::AreaLightComponent, Component::SphereLightComponent, AnalyticLightStore>,
            Writes<Component::AreaLightRuntime, Component::SphereLightRuntime, Core::ViewFamilyBakedShadows>>());
    graph.Add("GatherSkyAndFog",
        &GatherSkyAndFog,
        Access<
            Reads<Component::SkyboxComponent, Component::SkyboxRuntime, Component::VolumetricFogComponent>,
            Writes<Core::ViewFamilySettings, Core::ViewFamilyFog>>());
    graph.Add("GatherRenderables",
        &GatherRenderables,
        Access<
            Reads<IMaterialStore, InstanceRecords>,
            Writes<InstanceDirty, ModelStore, MaterialUploadDirty, Core::ViewFamilyRenderables>>());
    graph.Add("GatherLights",
        &GatherLights,
        Access<
            Reads<Component::MeshRuntime, Component::ProbeBakeHiddenFlag, Component::DirectionalLightComponent, Component::RenderWorldTransformRuntime, Component::AreaLightComponent, Component::AreaLightRuntime, Component::SphereLightComponent, Component::SphereLightRuntime, IMaterialStore, InstanceRecords, InstanceWorldBounds, Core::ViewFamilyBakedShadows, Core::RenderView>,
            Writes<AnalyticLightStore, TriLightStore, MaterialChangedDirty, Core::ViewFamilyLights, LocalShadowSelection, InstanceBoundsChanges>>());
    graph.Add("GatherTextRenderables",
        &GatherTextRenderables,
        Access<
            Reads<Component::TextComponent, Component::TextRuntime, Component::RenderTransformRuntime, Component::TextFontRequest, Component::StableIdComponent, IMaterialStore>,
            Writes<Core::ViewFamilyText>>());
    graph.Add("GatherReflectionProbes",
        &GatherReflectionProbes,
        Access<
            Reads<Component::ReflectionProbeComponent, Component::ReflectionProbeRuntime, Component::RenderWorldTransformRuntime>,
            Writes<Core::ViewFamilyProbes>>());
    graph.Add("GatherLocalDDGIVolumes",
        &GatherLocalDDGIVolumes,
        Access<
            Reads<Component::LocalDDGIVolumeComponent, Component::RenderWorldTransformRuntime, Core::DDGIParams>,
            Writes<Core::ViewFamilyDDGIVolumes>>());
    graph.Add("ApplyPostProcessVolumes",
        &ApplyPostProcessVolumes,
        Access<
            Reads<Component::PostProcessVolumeComponent, Component::RenderWorldTransformRuntime, Core::RenderView>,
            Writes<Core::ViewFamilyFog, Core::ViewFamilySettings>>());
    graph.Add("GatherFogVolumes",
        &GatherFogVolumes,
        Access<
            Reads<Component::FogVolumeComponent, Component::RenderWorldTransformRuntime, Core::RenderView>,
            Writes<Core::ViewFamilyFog>>());
    if (state->debug.bVerifyStoresOnce) {
        graph.Add("VerifyStores",
            [](EngineContext* ctx, EngineState* state, SystemRegistry& registry) {
#ifdef WDEBUG
                VerifyAnalyticLightStore(state, registry);
                VerifyGeometryStores(state, registry);
#endif
                state->debug.bVerifyStoresOnce = false;
            },
            AccessSet::EXCLUSIVE);
    }
    graph.Add("GatherUIRenderables",
        &GatherUIRenderables,
        AccessSet::EXCLUSIVE);

#if WILL_EDITOR
    graph.Add("DrawEditorInterface",
        &DrawEditorInterface,
        AccessSet::EXCLUSIVE);
    graph.Add("GatherEditorSprites",
        &GatherEditorSprites,
        Access<
            Reads<Component::AreaLightComponent, Component::SphereLightComponent, Component::DirectionalLightComponent, Component::ReflectionProbeComponent, Component::LocalDDGIVolumeComponent, Component::PostProcessVolumeComponent, Component::FogVolumeComponent, Component::RenderWorldTransformRuntime, Component::StableIdComponent>,
            Writes<Core::ViewFamilySprites>>());
    graph.Add("GatherLightDebugDraws",
        &GatherLightDebugDraws,
        Access<
            Reads<Component::AreaLightComponent, Component::SphereLightComponent, Component::DirectionalLightComponent, Component::LocalDDGIVolumeComponent, Component::PostProcessVolumeComponent, Component::FogVolumeComponent, Component::ReflectionProbeComponent, Component::ReflectionProbeRuntime, Component::RenderWorldTransformRuntime>,
            Writes<Core::ViewFamilyDebugDraws>>());
    graph.Add("GatherCSMDebugDraws",
        &GatherCSMDebugDraws,
        Access<
            Reads<Core::RenderView, Core::ViewFamilyLights, Core::ViewFamilySettings>,
            Writes<Core::ViewFamilyDebugDraws>>());
    graph.Add("GatherLocalShadowDebugDraws",
        &GatherLocalShadowDebugDraws,
        Access<
            Reads<LocalShadowSelection>,
            Writes<Core::ViewFamilyDebugDraws>>());
#endif

#ifdef WDEBUG
    graph.Add("DebugRenderPhysics",
        &DebugRenderPhysics,
        Access<
            Reads<Component::MeshRuntime, Component::MultiframeDirtyRequest, Component::PhysicsBodyRuntime, Component::PhysicsBodyDesc, Component::DrawPhysicsDebugTag, Core::RenderView>,
            Writes<Physics::PhysicsWorld, Core::ViewFamilyDebugDraws>>());
#endif
}

void ScrubFrame(EngineContext* ctx, EngineState* state)
{
    SystemRegistry registry{state->registry, AccessSet::EXCLUSIVE, nullptr, "ScrubFrame"};
    ProbeBakeScrubFrame(ctx, state, registry);
    PlaytestScrubFrame(ctx, state, registry);
}

void EndFrame(EngineContext* ctx)
{
    ctx->gameplayArena.Get().Reset();
#if WILL_EDITOR
    ctx->editorArena.Get().Reset();
#endif
}
} // Engine
