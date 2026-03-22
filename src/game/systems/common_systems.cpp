//
// Created by William on 2026-03-21.
//

#include "common_systems.h"

#include "game/components/common_components.h"
#include "game/components/common/stable_id_component.h"

namespace Game
{
void ConnectCommonObservers(entt::registry& registry)
{
    registry.on_construct<Component::StableIdComponent>().connect<&Component::StableIdComponent::OnConstruct>();
    registry.on_update<Component::StableIdComponent>().connect<&Component::StableIdComponent::OnUpdate>();
    registry.on_destroy<Component::StableIdComponent>().connect<&Component::StableIdComponent::OnDestroy>();
}
} // Game