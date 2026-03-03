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

    RegisterComponent<Component::NameComponent>(componentRegistry, "NameComponent");
    RegisterComponent<Component::StableIdComponent>(componentRegistry, "StableIdComponent");

    RegisterComponent<Component::TransformComponent>(componentRegistry, "TransformComponent");
    RegisterComponent<Component::FreeCameraComponent>(componentRegistry, "FreeCameraComponent");
    RegisterComponent<Component::StaticMeshComponent>(componentRegistry, "StaticMeshComponent");

    RegisterComponent<Component::PhysicsBodyDesc>(componentRegistry, "PhysicsBodyDesc");
    RegisterComponent<Component::DrawPhysicsDebugTag>(componentRegistry, "DrawPhysicsDebugTag");

    RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, "MotionBlurMovementComponent");
    RegisterComponent<Component::AntiGravityTag>(componentRegistry, "AntiGravityTag");
    RegisterComponent<Component::FloorTag>(componentRegistry, "FloorTag");
}
} // Core