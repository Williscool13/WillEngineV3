//
// Created by William on 2026-10-24.
//

#ifndef WILL_ENGINE_BALLANCE_CANE_LIFT_SYSTEMS_H
#define WILL_ENGINE_BALLANCE_CANE_LIFT_SYSTEMS_H

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Ballance
{
/** Clips idle carts on at the boot, releases them at the end of their path, and recycles eaten or lost ones. */
void UpdateCaneLifts(Engine::EngineContext* ctx, Engine::EngineState* state);
} // Ballance

#endif //WILL_ENGINE_BALLANCE_CANE_LIFT_SYSTEMS_H
