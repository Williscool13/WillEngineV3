//
// Created by William on 2026-10-22.
//

#ifndef WILL_ENGINE_BALLANCE_CRUSHER_SYSTEMS_H
#define WILL_ENGINE_BALLANCE_CRUSHER_SYSTEMS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Ballance
{
void UpdateCrushers(Engine::EngineContext* ctx, Engine::EngineState* state);
} // Ballance

#endif //WILL_ENGINE_BALLANCE_CRUSHER_SYSTEMS_H
