//
// Created by William on 2026-09-05.
//

#include "ballance.h"

#include <glm/glm.hpp>

#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/components/camera_components.h"
#include "engine/components/component_registration.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_components.h"
#include "engine/systems/system_graph.h"
#include "game/ballance/fwd_components.h"
#include "game/ballance/state.h"
#include "game/ballance/breakable/breakable_component.h"
#include "game/ballance/breakable/breakable_systems.h"
#include "game/ballance/cane/cane_lift_component.h"
#include "game/ballance/cane/cane_lift_systems.h"
#include "game/ballance/checkpoint/checkpoint_component.h"
#include "game/ballance/checkpoint/checkpoint_systems.h"
#include "game/ballance/checkpoint/death_zone_component.h"
#include "game/ballance/checkpoint/player_spawn_component.h"
#include "game/ballance/crusher/crusher_component.h"
#include "game/ballance/crusher/crusher_systems.h"
#include "game/ballance/culler/culler_component.h"
#include "game/ballance/culler/culler_systems.h"
#include "game/ballance/debug/debug_components.h"
#include "game/ballance/debug/debug_system.h"
#include "game/ballance/lava/lava_component.h"
#include "game/ballance/lava/lava_systems.h"
#include "physics/physics_world.h"

namespace Ballance
{
void RegisterComponents(Engine::ComponentRegistry& componentRegistry)
{
    Engine::RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::AntiGravityTag>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::FloorTag>(componentRegistry, Engine::Origin::Game, false, false);

    Engine::RegisterComponent<Component::CheckpointComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::DeathZoneTag>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::PlayerSpawnComponent>(componentRegistry, Engine::Origin::Game, false, false);

    Engine::RegisterComponent<Component::BreakableComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::SelfDestructComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::ShatterOnContactTag>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::ShardTag>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::CrusherComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::ThroatValveComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::CrateFeederComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::CullerComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::LavaComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::CaneLiftComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::CaneCartComponent>(componentRegistry, Engine::Origin::Game, false, false);

    Engine::RegisterRuntimeState<Component::CrusherRuntime>(componentRegistry, Engine::Origin::Game);
    Engine::RegisterRuntimeState<Component::ThroatValveRuntime>(componentRegistry, Engine::Origin::Game);
    Engine::RegisterRuntimeState<Component::CrateFeederRuntime>(componentRegistry, Engine::Origin::Game);
    Engine::RegisterRuntimeState<Component::SelfDestructRuntime>(componentRegistry, Engine::Origin::Game);
    Engine::RegisterRuntimeState<Component::CaneLiftRuntime>(componentRegistry, Engine::Origin::Game);
    Engine::RegisterRuntimeState<Component::CaneCartRuntime>(componentRegistry, Engine::Origin::Game);
    Engine::RegisterRuntimeState<Component::ShardSleepRuntime>(componentRegistry, Engine::Origin::Game);
    Engine::RegisterRuntimeState<Component::DissolvingRuntime>(componentRegistry, Engine::Origin::Game);
    Engine::RegisterRuntimeState<Component::DissolveKeepTag>(componentRegistry, Engine::Origin::Game);
    Engine::RegisterRuntimeState<Component::DissolvedFlag>(componentRegistry, Engine::Origin::Game);
}

void RegisterCommands(Engine::EngineState* state)
{
    RegisterBreakableCommands(state);
    RegisterCrusherCommands(state);
}

void ConnectObservers(Engine::Registry& registry)
{
    ConnectCheckpointObservers(registry);
    ConnectBreakableObservers(registry);
}

void DisconnectObservers(Engine::Registry& registry)
{
    DisconnectCheckpointObservers(registry);
    DisconnectBreakableObservers(registry);
}

void CollectSystems(Engine::SystemGraph& graph)
{
    graph.Add("Ballance/AcquireBreakableAssets", &AcquireBreakableAssets, Engine::Access<
        Engine::Reads<Component::BreakableComponent>,
        Engine::Writes<Component::BreakableRuntime, Engine::AssetManager>>());
    graph.Add("Ballance/UpdateSelfDestruct", &UpdateSelfDestruct, Engine::SystemWhen::WorldRunning, Engine::AccessSet::EXCLUSIVE);
    graph.Add("Ballance/ShatterOnContact", &ShatterOnContact, Engine::SystemWhen::WorldRunning, Engine::AccessSet::EXCLUSIVE);
    graph.Add("Ballance/UpdateThroatValves", &UpdateThroatValves, Engine::SystemWhen::WorldRunning, Engine::Access<
        Engine::Reads<Component::ThroatValveComponent, Component::ShardTag, Component::PhysicsBodyRuntime, Physics::PhysicsWorld>,
        Engine::Writes<Component::ThroatValveRuntime>>());
    graph.Add("Ballance/UpdateCrateFeeders", &UpdateCrateFeeders, Engine::SystemWhen::WorldRunning, Engine::AccessSet::EXCLUSIVE);
    graph.Add("Ballance/UpdateCrushers", &UpdateCrushers, Engine::SystemWhen::WorldRunning, Engine::AccessSet::EXCLUSIVE);
    graph.Add("Ballance/LavaContacts", &LavaContacts, Engine::SystemWhen::WorldRunning, Engine::AccessSet::EXCLUSIVE);
    graph.Add("Ballance/UpdateCuller", &UpdateCuller, Engine::SystemWhen::WorldRunning, Engine::AccessSet::EXCLUSIVE);
    graph.Add("Ballance/UpdateDissolving", &UpdateDissolving, Engine::SystemWhen::WorldRunning, Engine::Access<
        Engine::Reads<Component::HierarchyComponent, Component::DissolveKeepTag>,
        Engine::Writes<Component::DissolvingRuntime, Component::TransformComponent, Component::DirtyTransformRequest, Component::DissolvedFlag>>());
    graph.Add("Ballance/UpdateCaneLifts", &UpdateCaneLifts, Engine::SystemWhen::WorldRunning, Engine::AccessSet::EXCLUSIVE);

    graph.Add("Ballance/DebugProcessPhysicsCollisions", &DebugProcessPhysicsCollisions, Engine::SystemWhen::Gameplay, {
        .reads = {Engine::TypeSID<Component::FloorTag>(), "state.physics.resolvedAddedEvents"_sid},
        .writes = {Engine::TypeSID<Component::AntiGravityTag>()},
    });
    graph.Add("Ballance/DebugApplyGroundForces", &DebugApplyGroundForces, Engine::SystemWhen::Gameplay, Engine::Access<
        Engine::Reads<Component::AntiGravityTag, Component::PhysicsBodyRuntime>,
        Engine::Writes<Physics::PhysicsWorld>>());
    graph.Add("Ballance/CheckpointUpdate", &CheckpointUpdate, Engine::SystemWhen::Gameplay, {
        .reads = {Engine::TypeSID<Component::CheckpointComponent>(), "state.physics.resolvedAddedEvents"_sid},
        .writes = {"state.scene.currentCheckpointId"_sid, "state.scene.currentCheckpointPriority"_sid},
    });
    graph.Add("Ballance/DeathZoneUpdate", &DeathZoneUpdate, Engine::SystemWhen::Gameplay, {
        .reads = {Engine::TypeSID<Component::DeathZoneTag>(), Engine::TypeSID<Component::CheckpointComponent>(), "ballance.playerController"_sid, "state.physics.resolvedAddedEvents"_sid, "state.scene.currentCheckpointId"_sid},
        .writes = {Engine::TypeSID<Component::TransformComponent>(), Engine::TypeSID<Component::TeleportPhysicsTransformRequest>(), Engine::TypeSID<Component::SetVelocityRequest>(), "state.requests.renderReset"_sid},
    });
    graph.Add("Ballance/PlayerControllerUpdate", [](Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry) {
        PhysicsPlayerController& playerController = ctx->GetGameState<State>()->playerController;
        if (playerController.GetCharacter()) {
            playerController.Update(ctx, state, registry);
        }
    }, Engine::SystemWhen::Gameplay, {
        .reads = {Engine::TypeSID<Component::GameCameraTag>(), Engine::TypeSID<Component::PhysicsBodyRuntime>(), Engine::TypeSID<Component::DynamicPhysicsBodyRuntime>(), "state.input"_sid, "state.inputContext"_sid, "state.playtest.cameraOverride"_sid, "state.projectConfig"_sid, "state.physics.interpolationAlpha"_sid, "ctx.windowContext"_sid},
        .writes = {Engine::TypeSID<Component::CameraRuntime>(), Engine::TypeSID<Component::TransformComponent>(), Engine::TypeSID<Physics::PhysicsWorld>(), "ballance.playerController"_sid},
    });

    graph.Add("Ballance/DebugUpdate", &DebugUpdate, Engine::AccessSet::EXCLUSIVE);
}

void PrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer, Engine::SystemRegistry& registry)
{
#ifdef WDEBUG
    DebugRender(ctx, state, frameBuffer, registry);
#endif
}

void PlayStart(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Engine::SystemRegistry registry{state->registry, Engine::AccessSet::EXCLUSIVE, nullptr, "Lifecycle/PlayStart"};
    glm::vec3 spawnPosition{0.0f, 3.0f, 0.0f};
    int32_t bestPriority = INT32_MIN;
    auto spawnView = registry.View<Component::PlayerSpawnComponent, Component::TransformComponent>();
    for (auto entity : spawnView) {
        auto& spawn = spawnView.get<Component::PlayerSpawnComponent>(entity);
        if (spawn.priority > bestPriority) {
            bestPriority = spawn.priority;
            spawnPosition = spawnView.get<Component::TransformComponent>(entity).translation + spawn.offset;
        }
    }

    ctx->GetGameState<State>()->playerController.Initialize(state, ctx, spawnPosition, registry);
}

void PlayStop(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    Engine::SystemRegistry registry{state->registry, Engine::AccessSet::EXCLUSIVE, nullptr, "Lifecycle/PlayStop"};
    PhysicsPlayerController& playerController = ctx->GetGameState<State>()->playerController;
    if (playerController.GetCharacter()) {
        playerController.Shutdown(ctx->physicsWorld, registry);
    }
}
} // Ballance
