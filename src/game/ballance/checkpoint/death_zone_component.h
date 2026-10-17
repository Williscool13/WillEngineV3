//
// Created by William on 2026-03-28.
//

#ifndef WILL_ENGINE_DEATH_ZONE_COMPONENT_H
#define WILL_ENGINE_DEATH_ZONE_COMPONENT_H

#include <entt/entt.hpp>

#include "engine/component_registry.h"
#include "engine/components/component_types.h"
#include "game/ballance/fwd_components.h"

namespace Core { struct ViewFamily; }

namespace Ballance::Component
{
struct DeathZoneComponent
{
    static constexpr const char* COMPONENT_DOMAIN = BALLANCE_DOMAIN;
    static constexpr const char* COMPONENT_NAME = "DeathZoneComponent";

    static Engine::ComponentEditorResult DrawEditor(Core::ViewFamily& viewFamily, Engine::EditContext& edit, const char* name);
};
}

#endif //WILL_ENGINE_DEATH_ZONE_COMPONENT_H
