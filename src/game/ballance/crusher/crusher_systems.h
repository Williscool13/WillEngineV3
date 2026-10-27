//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_BALLANCE_CRUSHER_SYSTEMS_H
#define WILL_ENGINE_BALLANCE_CRUSHER_SYSTEMS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
class SystemRegistry;
}

namespace Ballance
{
void UpdateThroatValves(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void UpdateCrateFeeders(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void UpdateCrushers(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

void RegisterCrusherCommands(Engine::EngineState* state);
} // Ballance

#endif //WILL_ENGINE_BALLANCE_CRUSHER_SYSTEMS_H
