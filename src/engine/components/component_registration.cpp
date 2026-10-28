//
// Created by William on 2026-02-26.
//

#include "component_registration.h"

#include "engine/logging/engine_assert.h"
#include "engine/components/camera_components.h"
#include "engine/components/common_components.h"
#include "engine/components/common/stable_id_component.h"
#include "engine/components/core_components.h"
#include "engine/components/debug_gizmo_component.h"
#include "engine/components/editor_components.h"
#include "engine/components/motion/path_mover_component.h"
#include "engine/components/motion/rotate_in_place_component.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/components/physics/physics_components.h"
#include "engine/components/render/procedural_mesh_component.h"
#include "engine/components/render/spline_mesh_component.h"
#include "engine/components/render/module_mesh_component.h"
#include "engine/components/render/light_components.h"
#include "engine/components/render/local_ddgi_volume_component.h"
#include "engine/components/render/post_process_volume_component.h"
#include "engine/components/render/reflection_probe_component.h"
#include "engine/components/render/static_mesh_component.h"
#include "engine/components/render/static_mesh_primitive_component.h"
#include "engine/components/render/text_component.h"
#include "engine/components/render/text3d_component.h"
#include "engine/include/engine_context.h"
#include "engine/serialization/text_reader.h"
#include "engine/serialization/text_writer.h"
#include "physics/physics_world.h"

namespace Engine
{
template<typename Entry>
static void RemoveGameEntries(Core::Vector<Entry>& entries, Core::Map<StringID, size_t>& mapping)
{
    for (size_t i = entries.Size(); i-- > 0;) {
        if (entries[i].origin == Origin::Game) {
            entries.RemoveAt(i);
        }
    }

    mapping.Clear();
    for (size_t i = 0; i < entries.Size(); ++i) {
        mapping[entries[i].typeId] = i;
    }
}

void ClearGameComponents(ComponentRegistry& componentRegistry)
{
    RemoveGameEntries(componentRegistry.registry, componentRegistry.registryMapping);
    RemoveGameEntries(componentRegistry.runtimeState, componentRegistry.runtimeStateMapping);
}

/** Velocity lives in the physics world; a load requests it back once the body exists. */
static void RegisterBodyVelocityState(ComponentRegistry& componentRegistry)
{
    const StringID typeId = "PhysicsBodyVelocity"_sid;
    componentRegistry.runtimeStateMapping[typeId] = componentRegistry.runtimeState.Size();
    componentRegistry.runtimeState.PushBack(RuntimeStateEntry{
        .typeId = typeId,
        .name = "PhysicsBodyVelocity",
        .has = [](const Engine::SystemRegistry& reg, entt::entity e) -> bool { return reg.Has<Component::PhysicsBodyRuntime, Component::DynamicPhysicsBodyRuntime>(e); },
        .serialize = [](const Engine::SystemRegistry& reg, entt::entity e, TextWriter& w) {
            const Physics::PhysicsWorld* physicsWorld = &reg.Service<Physics::PhysicsWorld>();
            const Physics::BodyHandle body = reg.Get<Component::PhysicsBodyRuntime>(e).bodyID;
            w.Key("linearVelocity", physicsWorld->GetLinearVelocity(body));
            w.Key("angularVelocity", physicsWorld->GetAngularVelocity(body));
        },
        .deserialize = [](Engine::SystemRegistry& reg, entt::entity e, const TextReader& r) {
            reg.Set<Component::SetVelocityRequest>(e, Component::SetVelocityRequest{.linearVelocity = r.Vec3("linearVelocity"), .angularVelocity = r.Vec3("angularVelocity")});
        },
        .origin = Origin::Engine,
    });
}

void RegisterEngineComponents(Engine::ComponentRegistry& componentRegistry)
{
    ENGINE_ASSERT(Engine, componentRegistry.registry.Size() == 0, "engine components are registered once");

    RegisterComponent<Component::NameComponent>(componentRegistry, Origin::Engine, true, false);
    RegisterComponent<Component::StableIdComponent>(componentRegistry, Origin::Engine, true, true);
    RegisterComponent<Component::PrefabInstanceComponent>(componentRegistry, Origin::Engine, true, true);
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
    RegisterComponent<Component::VolumetricFogComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::FogVolumeComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::ReflectionProbeComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::LocalDDGIVolumeComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::PostProcessVolumeComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::ProceduralMeshComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::SplineMeshComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::ModuleMeshComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::Text3DComponent>(componentRegistry, Origin::Engine, false, false);

    RegisterComponent<Component::PhysicsBodyDesc>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::DrawPhysicsDebugTag>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::ConveyorComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::PathMoverComponent>(componentRegistry, Origin::Engine, false, false);
    RegisterComponent<Component::RotateInPlaceComponent>(componentRegistry, Origin::Engine, false, false);

    RegisterComponent<Component::DebugGizmoComponent>(componentRegistry, Origin::Engine, false, false);

    RegisterRuntimeState<Component::DoNotSerializeTag>(componentRegistry, Origin::Engine);
    RegisterRuntimeState<Component::PathMoverRuntime>(componentRegistry, Origin::Engine);
    RegisterRuntimeState<Component::PhysicsMotionOverrideRuntime>(componentRegistry, Origin::Engine);
    RegisterRuntimeState<Component::PhysicsSuspendedFlag>(componentRegistry, Origin::Engine);
    RegisterRuntimeState<Component::ConveyorHaltedFlag>(componentRegistry, Origin::Engine);
    RegisterBodyVelocityState(componentRegistry);
}
} // Engine
