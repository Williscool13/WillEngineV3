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

namespace Game::System
{
void GatherRenderables(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer, const Render::FrameResources* frameResources);
} // Game

#endif //WILL_ENGINE_GATHER_RENDERABLES_COMPONENT_H
