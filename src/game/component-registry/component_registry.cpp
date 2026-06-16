//
// Created by William on 2026-02-26.
//

#include "component_registry.h"

#include "game/components/camera_components.h"
#include "game/components/character_components.h"
#include "game/components/common_components.h"
#include "game/components/common/stable_id_component.h"
#include "game/components/core_components.h"
#include "game/components/debug_components.h"
#include "game/components/debug_gizmo_component.h"
#include "game/components/editor_components.h"
#include "game/components/physics/physics_body_desc.h"
#include "game/components/physics/physics_components.h"
#include "game/components/render/procedural_mesh_component.h"
#include "game/components/render/spline_mesh_component.h"
#include "game/components/gameplay/checkpoint_component.h"
#include "game/components/gameplay/death_zone_component.h"
#include "game/components/gameplay/path_mover_component.h"
#include "game/components/gameplay/rotate_in_place_component.h"
#include "game/components/gameplay/player_spawn_component.h"
#include "game/components/render/light_components.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/text_component.h"

namespace Game
{
void RegisterComponents(Engine::ComponentRegistry& componentRegistry)
{
    componentRegistry.registry.Clear();

    RegisterComponent<Component::NameComponent>(componentRegistry, "NameComponent");
    RegisterComponent<Component::StableIdComponent>(componentRegistry, "StableIdComponent");
    RegisterComponent<Component::PrefabInstanceComponent>(componentRegistry, "PrefabInstanceComponent");
    RegisterComponent<Component::EntityFolderComponent>(componentRegistry, "EntityFolderComponent");

    RegisterComponent<Component::TransformComponent>(componentRegistry, "TransformComponent");
    RegisterComponent<Component::FreeCameraComponent>(componentRegistry, "FreeCameraComponent");
    RegisterComponent<Component::StaticMeshComponent>(componentRegistry, "StaticMeshComponent");
    RegisterComponent<Component::TextComponent>(componentRegistry, "TextComponent");
    RegisterComponent<Component::PointLightComponent>(componentRegistry, "PointLightComponent");
    RegisterComponent<Component::AreaLightComponent>(componentRegistry, "AreaLightComponent");
    RegisterComponent<Component::DirectionalLightComponent>(componentRegistry, "DirectionalLightComponent");
    RegisterComponent<Component::ProceduralMeshComponent>(componentRegistry, "ProceduralMeshComponent");
    RegisterComponent<Component::SplineMeshComponent>(componentRegistry, "SplineMeshComponent");

    RegisterComponent<Component::CharacterPhysicsComponent>(componentRegistry, "CharacterPhysicsComponent");
    RegisterComponent<Component::PhysicsBodyDesc>(componentRegistry, "PhysicsBodyDesc");
    RegisterComponent<Component::DrawPhysicsDebugTag>(componentRegistry, "DrawPhysicsDebugTag");

    RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, "MotionBlurMovementComponent");
    RegisterComponent<Component::AntiGravityTag>(componentRegistry, "AntiGravityTag");
    RegisterComponent<Component::FloorTag>(componentRegistry, "FloorTag");

    RegisterComponent<Component::DebugGizmoComponent>(componentRegistry, "DebugGizmoComponent");
    RegisterComponent<Component::CheckpointComponent>(componentRegistry, "CheckpointComponent");
    RegisterComponent<Component::DeathZoneComponent>(componentRegistry, "DeathZoneComponent");
    RegisterComponent<Component::PlayerSpawnComponent>(componentRegistry, "PlayerSpawnComponent");
    RegisterComponent<Component::PathMoverComponent>(componentRegistry, "PathMoverComponent");
    RegisterComponent<Component::RotateInPlaceComponent>(componentRegistry, "RotateInPlaceComponent");
}
} // Core
