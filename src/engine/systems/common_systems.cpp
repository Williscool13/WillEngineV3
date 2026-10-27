//
// Created by William on 2026-03-21.
//

#include "common_systems.h"

#include "engine/components/common_components.h"
#include "engine/components/common/stable_id_component.h"

namespace Engine
{
void ConnectCommonObservers(Engine::Registry& registry)
{
    registry.ConnectConstruct<Component::StableIdComponent, &Component::StableIdComponent::OnConstruct>();
    registry.ConnectUpdate<Component::StableIdComponent, &Component::StableIdComponent::OnUpdate>();
    registry.ConnectDestroy<Component::StableIdComponent, &Component::StableIdComponent::OnDestroy>();
}

void DisconnectCommonObservers(Engine::Registry& registry)
{
    registry.DisconnectConstruct<Component::StableIdComponent, &Component::StableIdComponent::OnConstruct>();
    registry.DisconnectUpdate<Component::StableIdComponent, &Component::StableIdComponent::OnUpdate>();
    registry.DisconnectDestroy<Component::StableIdComponent, &Component::StableIdComponent::OnDestroy>();
}
} // Engine