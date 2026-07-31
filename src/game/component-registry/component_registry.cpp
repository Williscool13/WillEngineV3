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
#include "game/components/render/module_mesh_component.h"
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

    RegisterComponent<Component::NameComponent>(componentRegistry, true, false);
    RegisterComponent<Component::StableIdComponent>(componentRegistry, true, true);
    RegisterComponent<Component::PrefabInstanceComponent>(componentRegistry, false, false);
    RegisterComponent<Component::EntityFolderComponent>(componentRegistry, true, true);
    RegisterComponent<Component::SceneFolderComponent>(componentRegistry, true, false);

    RegisterComponent<Component::TransformComponent>(componentRegistry, false, false);
    RegisterComponent<Component::HierarchyComponent>(componentRegistry, true, true);
    RegisterComponent<Component::FreeCameraComponent>(componentRegistry, false, false);
    RegisterComponent<Component::StaticMeshComponent>(componentRegistry, false, false);
    RegisterComponent<Component::StaticMeshPrimitiveComponent>(componentRegistry, false, false);
    RegisterComponent<Component::TextComponent>(componentRegistry, false, false);
    RegisterComponent<Component::AreaLightComponent>(componentRegistry, false, false);
    RegisterComponent<Component::SphereLightComponent>(componentRegistry, false, false);
    RegisterComponent<Component::DirectionalLightComponent>(componentRegistry, false, false);
    RegisterComponent<Component::SkyboxComponent>(componentRegistry, false, false);
    RegisterComponent<Component::ReflectionProbeComponent>(componentRegistry, false, false);
    RegisterComponent<Component::ProceduralMeshComponent>(componentRegistry, false, false);
    RegisterComponent<Component::SplineMeshComponent>(componentRegistry, false, false);
    RegisterComponent<Component::ModuleMeshComponent>(componentRegistry, false, false);
    RegisterComponent<Component::Text3DComponent>(componentRegistry, false, false);

    RegisterComponent<Component::CharacterPhysicsComponent>(componentRegistry, false, false);
    RegisterComponent<Component::PhysicsBodyDesc>(componentRegistry, false, false);
    RegisterComponent<Component::DrawPhysicsDebugTag>(componentRegistry, false, false);

    RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, false, false);
    RegisterComponent<Component::AntiGravityTag>(componentRegistry, false, false);
    RegisterComponent<Component::FloorTag>(componentRegistry, false, false);

    RegisterComponent<Component::DebugGizmoComponent>(componentRegistry, false, false);
    RegisterComponent<Component::CheckpointComponent>(componentRegistry, false, false);
    RegisterComponent<Component::DeathZoneComponent>(componentRegistry, false, false);
    RegisterComponent<Component::PlayerSpawnComponent>(componentRegistry, false, false);
    RegisterComponent<Component::PathMoverComponent>(componentRegistry, false, false);
    RegisterComponent<Component::RotateInPlaceComponent>(componentRegistry, false, false);
}
} // Core
