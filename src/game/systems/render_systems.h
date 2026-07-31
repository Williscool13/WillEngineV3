//
// Created by William on 2025-12-26.
//

#ifndef WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H
#define WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H

#include <entt/entt.hpp>

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

namespace Game
{
void ConnectRenderObservers(entt::registry& registry);
void DisconnectRenderObservers(entt::registry& registry);

// Editor-only (generate while engine is running)
void ModelHotReload(Engine::EngineContext* ctx, Engine::EngineState* state);
void FontHotReload(Engine::EngineContext* ctx, Engine::EngineState* state);
void TextureHotReload(Engine::EngineContext* ctx, Engine::EngineState* state);
void CubemapHotReload(Engine::EngineContext* ctx, Engine::EngineState* state);

void StaticMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state);
void ReflectionProbePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state);
/** Upgrades a stand-in/unbaked probe to its baked content once its first bake lands in the probe registry; re-enters the kickoff flow. */
void ReflectionProbeBakeUpgrade(Engine::EngineContext* ctx, Engine::EngineState* state);
void StaticMeshPrimitivePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state);
void ProceduralMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state);
void SplineMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state);
void ModuleMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state);

void StaticMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state);
void ReflectionProbeLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state);
void StaticMeshPrimitiveLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state);
void ProceduralMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state);
void SplineMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state);
void ModuleMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state);
void TextFontPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state);
void Text3DGeneratePendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state);
void Text3DLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state);

void MarkRenderTransformsDirty(Engine::EngineContext* ctx, Engine::EngineState* state);

/**
 * Resolves every dirty entity's world transform into WorldTransformComponent.
 * @param ctx
 * @param state
 */
void ResolveWorldTransforms(Engine::EngineContext* ctx, Engine::EngineState* state);

void UpdateUIPointerState(Engine::EngineContext* ctx, Engine::EngineState* state);

void RenderPrepareTransforms(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void GatherRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void GatherTextRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void GatherUIRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void GatherLights(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);

/** Bake-time hide: tags entities flagged exclude-from-probe-bake plus dynamic/kinematic movers so gather drops them from raster, TLAS, and lights. */
void ApplyProbeBakeHideSet(Engine::EngineContext* ctx, Engine::EngineState* state);

/** Removes the transient ProbeBakeHiddenTag from every entity, restoring normal gather. */
void ClearProbeBakeHideSet(Engine::EngineContext* ctx, Engine::EngineState* state);
void GatherReflectionProbes(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void GatherEditorSprites(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void GatherLightDebugDraws(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
} // Game

#endif //WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H
