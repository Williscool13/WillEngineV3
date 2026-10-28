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
class SystemRegistry;
}

namespace Ballance
{
/** Removes the entity's physics body and starts it sinking, shrinking and tilting, through RunExclusive; a no-op when it is already dissolving, and the first of several calls before they apply wins. */
void StartDissolving(entt::entity entity, float sinkDepth, float duration, float tiltDegrees, Engine::SystemRegistry& registry, float shrinkStart = 0.8f);

/** Starts dissolving every dynamic body that newly touched a LavaComponent this frame. */
void LavaContacts(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);

/** Sinks, shrinks and tilts dissolving entities, destroying them (and their direct children) when done unless they carry a DissolveKeepTag. */
void UpdateDissolving(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry);
} // Ballance

#endif //WILL_ENGINE_BALLANCE_LAVA_SYSTEMS_H
