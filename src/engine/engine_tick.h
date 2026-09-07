//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_ENGINE_TICK_H
#define WILL_ENGINE_ENGINE_TICK_H

#include <entt/entt.hpp>

namespace Core
{
struct FrameBuffer;
}

namespace Engine
{
struct EngineContext;
struct EngineState;
class SystemGraph;

void ConnectEngineObservers(entt::registry& registry);

void CollectPreUpdate(EngineContext* ctx, EngineState* state, SystemGraph& graph);

void CollectPostUpdate(EngineContext* ctx, EngineState* state, SystemGraph& graph);

void CollectPrepareFrame(EngineContext* ctx, EngineState* state, SystemGraph& graph);

void ScrubFrame(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer);

void EndFrame(EngineContext* ctx);
} // Engine

#endif //WILL_ENGINE_ENGINE_TICK_H
