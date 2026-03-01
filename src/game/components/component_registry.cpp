//
// Created by William on 2026-02-26.
//

#include "component_registry.h"

#include "camera_components.h"
#include "debug_components.h"
#include "physics_components.h"

namespace Game
{
void RegisterComponents(ComponentRegistry& componentRegistry)
{
    componentRegistry.registry.Clear();

    RegisterComponent<Component::TransformComponent>(componentRegistry, "TransformComponent"_sid, "TransformComponent");
    RegisterComponent<Component::FreeCameraComponent>(componentRegistry, "FreeCameraComponent"_sid, "FreeCameraComponent");
    RegisterComponent<Component::StaticMeshComponent>(componentRegistry, "StaticMeshComponent"_sid, "StaticMeshComponent");
    RegisterComponent<Component::StableIdComponent>(componentRegistry, "StableIdComponent"_sid, "StableIdComponent");

    RegisterComponent<Component::PhysicsBodyDesc>(componentRegistry, "PhysicsBodyDesc"_sid, "PhysicsBodyDesc");

    RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, "MotionBlurMovementComponent"_sid, "MotionBlurMovementComponent");
    RegisterComponent<Component::AntiGravityComponent>(componentRegistry, "AntiGravityComponent"_sid, "AntiGravityComponent");
    RegisterComponent<Component::FloorComponent>(componentRegistry, "FloorComponent"_sid, "FloorComponent");
}
} // Core