//
// Created by William on 2026-09-05.
//

#include "component_registration.h"

#include "engine/components/component_registration.h"
#include "game/components/debug_components.h"
#include "game/components/checkpoint_component.h"
#include "game/components/death_zone_component.h"
#include "game/components/path_mover_component.h"
#include "game/components/player_spawn_component.h"
#include "game/components/rotate_in_place_component.h"

namespace Game
{
void RegisterGameComponents(Engine::ComponentRegistry& componentRegistry)
{
    Engine::RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, false, false);
    Engine::RegisterComponent<Component::AntiGravityTag>(componentRegistry, false, false);
    Engine::RegisterComponent<Component::FloorTag>(componentRegistry, false, false);

    Engine::RegisterComponent<Component::CheckpointComponent>(componentRegistry, false, false);
    Engine::RegisterComponent<Component::DeathZoneComponent>(componentRegistry, false, false);
    Engine::RegisterComponent<Component::PlayerSpawnComponent>(componentRegistry, false, false);
    Engine::RegisterComponent<Component::PathMoverComponent>(componentRegistry, false, false);
    Engine::RegisterComponent<Component::RotateInPlaceComponent>(componentRegistry, false, false);
}
} // Game
