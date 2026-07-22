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
#include "game/components/render/reflection_probe_component.h"
#include "game/components/render/static_mesh_component.h"
#include "game/components/render/static_mesh_primitive_component.h"
#include "game/components/render/text_component.h"
#include "game/components/render/text3d_component.h"

namespace Game
{
void RegisterComponents(Engine::ComponentRegistry& componentRegistry)
{
    componentRegistry.registry.Clear();

    RegisterComponent<Component::NameComponent>(componentRegistry, "NameComponent", true, false);
    RegisterComponent<Component::StableIdComponent>(componentRegistry, "StableIdComponent", true, true);
    RegisterComponent<Component::PrefabInstanceComponent>(componentRegistry, "PrefabInstanceComponent", false, false);
    RegisterComponent<Component::EntityFolderComponent>(componentRegistry, "EntityFolderComponent", true, true);
    RegisterComponent<Component::SceneFolderComponent>(componentRegistry, "SceneFolderComponent", true, false);

    RegisterComponent<Component::TransformComponent>(componentRegistry, "TransformComponent", false, false);
    RegisterComponent<Component::HierarchyComponent>(componentRegistry, "HierarchyComponent", true, true);
    RegisterComponent<Component::FreeCameraComponent>(componentRegistry, "FreeCameraComponent", false, false);
    RegisterComponent<Component::StaticMeshComponent>(componentRegistry, "StaticMeshComponent", false, false);
    RegisterComponent<Component::StaticMeshPrimitiveComponent>(componentRegistry, "StaticMeshPrimitiveComponent", false, false);
    RegisterComponent<Component::TextComponent>(componentRegistry, "TextComponent", false, false);
    RegisterComponent<Component::AreaLightComponent>(componentRegistry, "AreaLightComponent", false, false);
    RegisterComponent<Component::SphereLightComponent>(componentRegistry, "SphereLightComponent", false, false);
    RegisterComponent<Component::DirectionalLightComponent>(componentRegistry, "DirectionalLightComponent", false, false);
    RegisterComponent<Component::ReflectionProbeComponent>(componentRegistry, "ReflectionProbeComponent", false, false);
    RegisterComponent<Component::ProceduralMeshComponent>(componentRegistry, "ProceduralMeshComponent", false, false);
    RegisterComponent<Component::SplineMeshComponent>(componentRegistry, "SplineMeshComponent", false, false);
    RegisterComponent<Component::Text3DComponent>(componentRegistry, "Text3DComponent", false, false);

    RegisterComponent<Component::CharacterPhysicsComponent>(componentRegistry, "CharacterPhysicsComponent", false, false);
    RegisterComponent<Component::PhysicsBodyDesc>(componentRegistry, "PhysicsBodyDesc", false, false);
    RegisterComponent<Component::DrawPhysicsDebugTag>(componentRegistry, "DrawPhysicsDebugTag", false, false);

    RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, "MotionBlurMovementComponent", false, false);
    RegisterComponent<Component::AntiGravityTag>(componentRegistry, "AntiGravityTag", false, false);
    RegisterComponent<Component::FloorTag>(componentRegistry, "FloorTag", false, false);

    RegisterComponent<Component::DebugGizmoComponent>(componentRegistry, "DebugGizmoComponent", false, false);
    RegisterComponent<Component::CheckpointComponent>(componentRegistry, "CheckpointComponent", false, false);
    RegisterComponent<Component::DeathZoneComponent>(componentRegistry, "DeathZoneComponent", false, false);
    RegisterComponent<Component::PlayerSpawnComponent>(componentRegistry, "PlayerSpawnComponent", false, false);
    RegisterComponent<Component::PathMoverComponent>(componentRegistry, "PathMoverComponent", false, false);
    RegisterComponent<Component::RotateInPlaceComponent>(componentRegistry, "RotateInPlaceComponent", false, false);
}
} // Core
