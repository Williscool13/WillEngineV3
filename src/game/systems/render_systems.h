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

void ResolveStaticMeshLoads(Engine::EngineContext* ctx, Engine::EngineState* state);
void ResolveTextLoads(Engine::EngineContext* ctx, Engine::EngineState* state);
void ResolveProceduralMeshLoads(Engine::EngineContext* ctx, Engine::EngineState* state);
void ResolveSplineMeshLoads(Engine::EngineContext* ctx, Engine::EngineState* state);

void MarkRenderTransformsDirty(Engine::EngineContext* ctx, Engine::EngineState* state);

void RenderPrepareTransforms(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void GatherRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void GatherTextRenderables(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
} // Game

#endif //WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H
