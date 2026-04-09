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
struct EngineState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
}

namespace Game
{
void ConnectRenderObservers(entt::registry& registry);

void ResolveStaticMeshLoads(Core::EngineContext* ctx, Engine::EngineState* state);
void ResolveProceduralMeshLoads(Core::EngineContext* ctx, Engine::EngineState* state);
void ResolveSplineMeshLoads(Core::EngineContext* ctx, Engine::EngineState* state);

void MarkRenderTransformsDirty(Core::EngineContext* ctx, Engine::EngineState* state);

void RenderPrepareTransforms(Core::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
void GatherRenderables(Core::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer);
} // Game

#endif //WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H
