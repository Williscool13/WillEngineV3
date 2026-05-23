//
// Created by William on 2026-03-21.
//

#include "common_systems.h"

#include "game/components/common_components.h"
#include "game/components/common/stable_id_component.h"
#include "game/components/gameplay/checkpoint_component.h"

namespace Game
{
void ConnectCommonObservers(entt::registry& registry)
{
    registry.on_construct<Component::StableIdComponent>().connect<&Component::StableIdComponent::OnConstruct>();
    registry.on_update<Component::StableIdComponent>().connect<&Component::StableIdComponent::OnUpdate>();
    registry.on_destroy<Component::StableIdComponent>().connect<&Component::StableIdComponent::OnDestroy>();

    registry.on_construct<Component::CheckpointComponent>().connect<&Component::CheckpointComponent::OnConstruct>();
}

void DisconnectCommonObservers(entt::registry& registry)
{
    registry.on_construct<Component::StableIdComponent>().disconnect<&Component::StableIdComponent::OnConstruct>();
    registry.on_update<Component::StableIdComponent>().disconnect<&Component::StableIdComponent::OnUpdate>();
    registry.on_destroy<Component::StableIdComponent>().disconnect<&Component::StableIdComponent::OnDestroy>();

    registry.on_construct<Component::CheckpointComponent>().disconnect<&Component::CheckpointComponent::OnConstruct>();
}
} // Game