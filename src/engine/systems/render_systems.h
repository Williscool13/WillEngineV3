//
// Created by William on 2025-12-26.
//

#ifndef WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H
#define WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H

#include "engine/core/registry.h"

namespace Render
{
struct FrameResources;
}

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Core
{
struct FrameBuffer;
}

namespace Engine
{
void ConnectRenderObservers(Engine::Registry& registry);
void DisconnectRenderObservers(Engine::Registry& registry);

/** Recomputes one entity's effective render state and writes it to the instance store. */
void EvaluateInstanceRenderState(Engine::EngineState* state, entt::entity entity, Engine::SystemRegistry& registry);
void EvaluateAllInstanceRenderStates(Engine::EngineState* state, Engine::SystemRegistry& registry);

// Editor-only (generate while engine is running)
void ModelHotReload(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void FontHotReload(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void TextureHotReload(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void CubemapHotReload(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void StaticMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void ReflectionProbePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
/** Upgrades a stand-in/unbaked probe to its baked content once its first bake lands in the probe registry; re-enters the kickoff flow. */
void ReflectionProbeBakeUpgrade(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

/** Loads baked shadow maps that match their lights, drops stale ones, and lists the resident ones on the view family. */
void ResolveBakedShadows(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
void StaticMeshPrimitivePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void ProceduralMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void SplineMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void ModuleMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void StaticMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void LightSurfaceResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void ReflectionProbeLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void StaticMeshPrimitiveLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void ProceduralMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void SplineMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void ModuleMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void TextFontPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void Text3DGeneratePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void Text3DLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void MarkRenderTransformsDirty(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

/**
 * Resolves every dirty entity's world transform into WorldTransformRuntime.
 * @param ctx
 * @param state
 */
void ResolveWorldTransforms(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void UpdateUIPointerState(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void RenderPrepareTransforms(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);

/** Serial pre-pass: rebinds emissive surface materials and writes instance render state so GatherRenderables can run with a const registry. */
void SyncLightSurfaces(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

/** Serial pre-pass: kicks the lazy cubemap load for skybox components so the gathers never mutate. */
void ResolveSkyboxCubemaps(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void GatherRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
void GatherTextRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
void GatherLights(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);

/** Bake-time hide: tags entities flagged exclude-from-probe-bake plus dynamic/kinematic movers so gather drops them from raster, TLAS, and lights. */
void ApplyProbeBakeHideSet(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

/** Removes the transient ProbeBakeHiddenFlag from every entity, restoring normal gather. */
void ClearProbeBakeHideSet(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
void GatherReflectionProbes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
void GatherLocalDDGIVolumes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
void ApplyPostProcessVolumes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
void GatherFogVolumes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
void GatherEditorSprites(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
void GatherLightDebugDraws(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);

void GatherCSMDebugDraws(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);

void GatherLocalShadowDebugDraws(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry);
} // Engine

#endif //WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H
