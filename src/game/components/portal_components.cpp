//
// Created by William on 2026-01-30.
//

#include "portal_components.h"

#include "engine/include/engine_context.h"
#include "engine/engine_api.h"
#include "game/components/core_components.h"
#include "spdlog/spdlog.h"

namespace Game::Component
{
PortalPair CreatePortalPair(Engine::EngineContext* ctx, Engine::EngineState* state, glm::vec3 posA, glm::quat rotA, glm::vec3 posB, glm::quat rotB)
{
    SPDLOG_WARN("[DebugSystem] Portals are deprecated and disabled");
    return {entt::null, entt::null};
}

void CreatePortalPlane(Engine::EngineContext* ctx, Engine::EngineState* state, glm::vec3 position, glm::quat rotation, glm::vec3 scale)
{
    SPDLOG_WARN("[DebugSystem] Portals are deprecated and disabled");
}
}
