//
// Created by William on 2026-01-30.
//

#ifndef WILL_ENGINE_PORTAL_COMPONENTS_H
#define WILL_ENGINE_PORTAL_COMPONENTS_H

#include <entt/entt.hpp>

#include "engine/asset_manager.h"
#include "render_components.h"
#include "render/static_mesh_component.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game::Component
{
struct PortalComponent
{
    entt::entity linkedPortal = entt::null;
    uint32_t stencilValue = 0;
};

struct PortalPair
{
    entt::entity portalA;
    entt::entity portalB;
};

struct PortalPlaneTag
{};

PortalPair CreatePortalPair(Engine::EngineContext* ctx, Engine::EngineState* state, glm::vec3 posA, glm::quat rotA, glm::vec3 posB, glm::quat rotB);

void CreatePortalPlane(Engine::EngineContext* ctx, Engine::EngineState* state, glm::vec3 position, glm::quat rotation, glm::vec3 scale);
}

#endif //WILL_ENGINE_PORTAL_COMPONENTS_H
