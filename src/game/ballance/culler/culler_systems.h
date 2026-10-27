//
// Created by William on 2026-10-27.
//

#ifndef WILL_ENGINE_BALLANCE_CULLER_SYSTEMS_H
#define WILL_ENGINE_BALLANCE_CULLER_SYSTEMS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
class SystemRegistry;
}

namespace Ballance
{
void UpdateCuller(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
} // Ballance

#endif //WILL_ENGINE_BALLANCE_CULLER_SYSTEMS_H
