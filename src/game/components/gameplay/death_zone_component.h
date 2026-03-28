//
// Created by William on 2026-03-28.
//

#ifndef WILL_ENGINE_DEATH_ZONE_COMPONENT_H
#define WILL_ENGINE_DEATH_ZONE_COMPONENT_H

#include <entt/entt.hpp>

#include "game/components/component_types.h"

namespace Core { struct ViewFamily; }

namespace Game::Component
{
struct DeathZoneComponent
{
    static ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, entt::registry& registry, entt::entity entity, const char* name);
};
}

#endif //WILL_ENGINE_DEATH_ZONE_COMPONENT_H
