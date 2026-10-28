//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_BALLANCE_H
#define WILL_ENGINE_BALLANCE_H

#include "engine/core/registry.h"

namespace Core
{
struct FrameBuffer;
}

namespace Engine
{
struct ComponentRegistry;
struct EngineContext;
struct EngineState;
class SystemGraph;
}

namespace Ballance
{
void RegisterComponents(Engine::ComponentRegistry& componentRegistry);
void RegisterCommands(Engine::EngineState* state);
void ConnectObservers(Engine::Registry& registry);
void DisconnectObservers(Engine::Registry& registry);
void CollectSystems(Engine::SystemGraph& graph);
void PrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, Core::FrameBuffer* frameBuffer);

void PlayStart(Engine::EngineContext* ctx, Engine::EngineState* state);
void PlayStop(Engine::EngineContext* ctx, Engine::EngineState* state);
} // Ballance

#endif //WILL_ENGINE_BALLANCE_H
