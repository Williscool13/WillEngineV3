//
// Created by William on 2025-12-26.
//

#ifndef WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H
#define WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H

namespace Render
{
struct FrameResources;
}

namespace Engine
{
struct GameState;
}

namespace Core
{
struct FrameBuffer;
struct EngineContext;
}

namespace Game
{
void ResolveStaticMeshLoads(Core::EngineContext* ctx, Engine::GameState* state);
void ResolveProceduralMeshLoads(Core::EngineContext* ctx, Engine::GameState* state);
void ResolveSplineMeshLoads(Core::EngineContext* ctx, Engine::GameState* state);

void RenderUpdate(Core::EngineContext* ctx, Engine::GameState* state);

void RenderPrepareTransforms(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);
void GatherRenderables(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer);
} // Game

#endif //WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H
