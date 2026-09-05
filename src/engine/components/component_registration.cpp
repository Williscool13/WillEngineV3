//
// Created by William on 2026-02-26.
//

#include "component_registration.h"

#include "engine/logging/engine_assert.h"
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
void ClearGameComponents(ComponentRegistry& componentRegistry)
{
    for (size_t i = componentRegistry.registry.Size(); i-- > 0;) {
        if (componentRegistry.registry[i].origin == Origin::Game) {
            componentRegistry.registry.RemoveAt(i);
        }
    }

    componentRegistry.registryMapping.Clear();
    for (size_t i = 0; i < componentRegistry.registry.Size(); ++i) {
        componentRegistry.registryMapping[componentRegistry.registry[i].typeId] = i;
    }
}

void RegisterEngineComponents(Engine::ComponentRegistry& componentRegistry)
{
    ENGINE_ASSERT(Engine, componentRegistry.registry.Size() == 0, "engine components are registered once");

    RegisterComponent<Component::NameComponent>(componentRegistry, Origin::Engine, true, false);
    RegisterComponent<Component::StableIdComponent>(componentRegistry, Origin::Engine, true, true);
    RegisterComponent<Component::PrefabInstanceComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::EntityFolderComponent>(componentRegistry, Origin::Engine, true, true);
    RegisterComponent<Component::SceneFolderComponent>(componentRegistry, Origin::Engine, true, false);

    RegisterComponent<Component::TransformComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::HierarchyComponent>(componentRegistry, Origin::Engine, true, true);
    RegisterComponent<Component::RenderFlagsComponent>(componentRegistry, Origin::Engine, true, true);
    RegisterComponent<Component::FreeCameraComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::StaticMeshComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::StaticMeshOverridesComponent>(componentRegistry, Origin::Engine, true, true);
    RegisterComponent<Component::StaticMeshPrimitiveComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::TextComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::AreaLightComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::SphereLightComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::DirectionalLightComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::SkyboxComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::ReflectionProbeComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::LocalDDGIVolumeComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::ProceduralMeshComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::SplineMeshComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::ModuleMeshComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::Text3DComponent>(componentRegistry, Origin::Engine, false, false);

    RegisterComponent<Component::CharacterPhysicsComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::PhysicsBodyDesc>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::DrawPhysicsDebugTag>(componentRegistry, Origin::Engine, false, false);

    RegisterComponent<Component::DebugGizmoComponent>(componentRegistry, Origin::Engine, false, false);
}
} // Engine
