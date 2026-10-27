//
// Created by William on 2026-10-24.
//

#ifndef WILL_ENGINE_BALLANCE_LAVA_SYSTEMS_H
#define WILL_ENGINE_BALLANCE_LAVA_SYSTEMS_H

#include <entt/entt.hpp>

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Ballance
{
/** Removes the entity's physics body and starts it sinking, shrinking and tilting; a no-op when it is already dissolving. */
void StartDissolving(Engine::EngineState* state, entt::entity entity, float sinkDepth, float duration, float tiltDegrees, float shrinkStart = 0.8f);

/** Starts dissolving every dynamic body that newly touched a LavaComponent this frame. */
void LavaContacts(Engine::EngineContext* ctx, Engine::EngineState* state);

/** Sinks, shrinks and tilts dissolving entities, destroying them (and their direct children) when done unless they carry a DissolveKeepTag. */
void UpdateDissolving(Engine::EngineContext* ctx, Engine::EngineState* state);
} // Ballance

#endif //WILL_ENGINE_BALLANCE_LAVA_SYSTEMS_H
