//
// Created by William on 2026-10-25.
//

#ifndef WILL_ENGINE_RUNTIME_SAVE_H
#define WILL_ENGINE_RUNTIME_SAVE_H

namespace Engine
{
struct EngineContext;
struct EngineState;

/** Writes `<user data>/saves/<name>.wsave` while running: loaded scenes with runtime state, body velocities, rng and run clock. */
bool SaveRuntime(EngineContext* ctx, EngineState* state, const char* name);

/** Needs the same scenes loaded. In Play the player respawns. */
bool LoadRuntime(EngineContext* ctx, EngineState* state, const char* name);
} // Engine

#endif //WILL_ENGINE_RUNTIME_SAVE_H
