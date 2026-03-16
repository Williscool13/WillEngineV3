//
// Created by William on 2026-02-26.
//

#include "component_registry.h"

#include "../components/camera_components.h"
#include "../components/debug_components.h"
#include "../components/physics_components.h"

namespace Game
{
void RegisterComponents(ComponentRegistry& componentRegistry)
{
    componentRegistry.registry.Clear();

    RegisterComponent<Component::NameComponent>(componentRegistry, "NameComponent");
    RegisterComponent<Component::StableIdComponent>(componentRegistry, "StableIdComponent");
    RegisterComponent<Component::EntityFolderComponent>(componentRegistry, "EntityFolderComponent");

    RegisterComponent<Component::TransformComponent>(componentRegistry, "TransformComponent");
    RegisterComponent<Component::FreeCameraComponent>(componentRegistry, "FreeCameraComponent");
    RegisterComponent<Component::StaticMeshComponent>(componentRegistry, "StaticMeshComponent");
    RegisterComponent<Component::ProceduralMeshComponent>(componentRegistry, "ProceduralMeshComponent");
    RegisterComponent<Component::SplineMeshComponent>(componentRegistry, "SplineMeshComponent");

    RegisterComponent<Component::PhysicsBodyDesc>(componentRegistry, "PhysicsBodyDesc");
    RegisterComponent<Component::DrawPhysicsDebugTag>(componentRegistry, "DrawPhysicsDebugTag");

    RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, "MotionBlurMovementComponent");
    RegisterComponent<Component::AntiGravityTag>(componentRegistry, "AntiGravityTag");
    RegisterComponent<Component::FloorTag>(componentRegistry, "FloorTag");
}
} // Core