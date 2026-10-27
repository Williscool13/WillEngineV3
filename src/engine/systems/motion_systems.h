//
// Created by William on 2026-10-17.
//

#ifndef WILL_ENGINE_MOTION_SYSTEMS_H
#define WILL_ENGINE_MOTION_SYSTEMS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
class SystemRegistry;

void UpdatePathMovers(EngineContext* ctx, EngineState* state, SystemRegistry& registry);
void UpdateRotateInPlace(EngineContext* ctx, EngineState* state, SystemRegistry& registry);
} // Engine

#endif //WILL_ENGINE_MOTION_SYSTEMS_H
