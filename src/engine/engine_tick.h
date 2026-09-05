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

void ConnectEngineObservers(entt::registry& registry);

void PreUpdate(EngineContext* ctx, EngineState* state);

void PostUpdate(EngineContext* ctx, EngineState* state);

void PrepareFrame(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer);

void ScrubFrame(EngineContext* ctx, EngineState* state, Core::FrameBuffer* frameBuffer);

void EndFrame(EngineContext* ctx);
} // Engine

#endif //WILL_ENGINE_ENGINE_TICK_H
