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
    Engine::RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::AntiGravityTag>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::FloorTag>(componentRegistry, Engine::Origin::Game, false, false);

    Engine::RegisterComponent<Component::CheckpointComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::DeathZoneComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::PlayerSpawnComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::PathMoverComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::RotateInPlaceComponent>(componentRegistry, Engine::Origin::Game, false, false);
}
} // Game
