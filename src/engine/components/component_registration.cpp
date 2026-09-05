//
// Created by William on 2026-02-26.
//

#include "component_registration.h"

#include "engine/components/camera_components.h"
#include "engine/components/character_components.h"
#include "engine/components/common_components.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/components/core_components.h"
#include "engine/components/debug_gizmo_component.h"
#include "engine/components/editor_components.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/components/physics/physics_components.h"
#include "engine/components/render/procedural_mesh_component.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/module_mesh_component.h"
#include "engine/components/render/light_components.h"
#include "engine/components/render/local_ddgi_volume_component.h"
#include "engine/components/render/reflection_probe_component.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/components/render/static_mesh_primitive_component.h"
#include "engine/components/render/text_component.h"
#include "engine/components/render/text3d_component.h"

namespace Engine
{
void RegisterEngineComponents(Engine::ComponentRegistry& componentRegistry)
{
    componentRegistry.registry.Clear();
    componentRegistry.registryMapping.Clear();

    RegisterComponent<Component::NameComponent>(componentRegistry, true, false);
    RegisterComponent<Component::StableIdComponent>(componentRegistry, true, true);
    RegisterComponent<Component::PrefabInstanceComponent>(componentRegistry, false, false);
    RegisterComponent<Component::EntityFolderComponent>(componentRegistry, true, true);
    RegisterComponent<Component::SceneFolderComponent>(componentRegistry, true, false);

    RegisterComponent<Component::TransformComponent>(componentRegistry, false, false);
    RegisterComponent<Component::HierarchyComponent>(componentRegistry, true, true);
    RegisterComponent<Component::RenderFlagsComponent>(componentRegistry, true, true);
    RegisterComponent<Component::FreeCameraComponent>(componentRegistry, false, false);
    RegisterComponent<Component::StaticMeshComponent>(componentRegistry, false, false);
    RegisterComponent<Component::StaticMeshOverridesComponent>(componentRegistry, true, true);
    RegisterComponent<Component::StaticMeshPrimitiveComponent>(componentRegistry, false, false);
    RegisterComponent<Component::TextComponent>(componentRegistry, false, false);
    RegisterComponent<Component::AreaLightComponent>(componentRegistry, false, false);
    RegisterComponent<Component::SphereLightComponent>(componentRegistry, false, false);
    RegisterComponent<Component::DirectionalLightComponent>(componentRegistry, false, false);
    RegisterComponent<Component::SkyboxComponent>(componentRegistry, false, false);
    RegisterComponent<Component::ReflectionProbeComponent>(componentRegistry, false, false);
    RegisterComponent<Component::LocalDDGIVolumeComponent>(componentRegistry, false, false);
    RegisterComponent<Component::ProceduralMeshComponent>(componentRegistry, false, false);
    RegisterComponent<Component::SplineMeshComponent>(componentRegistry, false, false);
    RegisterComponent<Component::ModuleMeshComponent>(componentRegistry, false, false);
    RegisterComponent<Component::Text3DComponent>(componentRegistry, false, false);

    RegisterComponent<Component::CharacterPhysicsComponent>(componentRegistry, false, false);
    RegisterComponent<Component::PhysicsBodyDesc>(componentRegistry, false, false);
    RegisterComponent<Component::DrawPhysicsDebugTag>(componentRegistry, false, false);

    RegisterComponent<Component::DebugGizmoComponent>(componentRegistry, false, false);
}
} // Engine
