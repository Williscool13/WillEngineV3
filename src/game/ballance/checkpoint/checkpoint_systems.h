//
// Created by William on 2026-03-26.
//

#ifndef WILL_ENGINE_BALLANCE_CHECKPOINT_SYSTEMS_H
#define WILL_ENGINE_BALLANCE_CHECKPOINT_SYSTEMS_H

#include "engine/core/registry.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Ballance
{
void ConnectCheckpointObservers(Engine::Registry& registry);
void DisconnectCheckpointObservers(Engine::Registry& registry);

void CheckpointUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);
void DeathZoneUpdate(Engine::EngineContext* ctx, Engine::EngineState* state);
} // Ballance

#endif //WILL_ENGINE_BALLANCE_CHECKPOINT_SYSTEMS_H
