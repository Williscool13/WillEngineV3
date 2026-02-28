//
// Created by William on 2026-02-26.
//

#include "component_registry.h"

#include "camera_components.h"
#include "debug_components.h"
#include "physics_components.h"

namespace Core
{
void RegisterComponents(ComponentRegistry& componentRegistry)
{
    componentRegistry.registry.Clear();

    Core::RegisterComponent<Game::Component::TransformComponent>(componentRegistry, "TransformComponent"_sid);
    Core::RegisterComponent<Game::Component::FreeCameraComponent>(componentRegistry, "FreeCameraComponent"_sid);
    Core::RegisterComponent<Game::Component::StaticMeshComponent>(componentRegistry, "StaticMeshComponent"_sid);
    Core::RegisterComponent<Game::Component::StableIdComponent>(componentRegistry, "StableIdComponent"_sid);

    Core::RegisterComponent<Game::Component::PhysicsBodyDesc>(componentRegistry, "PhysicsBodyDesc"_sid);

    Core::RegisterComponent<Game::Component::MotionBlurMovementComponent>(componentRegistry, "MotionBlurMovementComponent"_sid);
    Core::RegisterComponent<Game::Component::AntiGravityComponent>(componentRegistry, "AntiGravityComponent"_sid);
    Core::RegisterComponent<Game::Component::FloorComponent>(componentRegistry, "FloorComponent"_sid);
}
} // Core