//
// Created by William on 2026-09-05.
//

#include "ballance.h"

#include <glm/glm.hpp>

#include "engine/engine_api.h"
#include "engine/include/engine_context.h"
#include "engine/components/component_registration.h"
#include "engine/components/core_components.h"
#include "engine/systems/system_graph.h"
#include "game/ballance/fwd_components.h"
#include "game/ballance/state.h"
#include "game/ballance/breakable/breakable_component.h"
#include "game/ballance/breakable/breakable_systems.h"
#include "game/ballance/checkpoint/checkpoint_component.h"
#include "game/ballance/checkpoint/checkpoint_systems.h"
#include "game/ballance/checkpoint/death_zone_component.h"
#include "game/ballance/checkpoint/player_spawn_component.h"
#include "game/ballance/crusher/crusher_component.h"
#include "game/ballance/crusher/crusher_systems.h"
#include "game/ballance/debug/debug_components.h"
#include "game/ballance/debug/debug_system.h"
#include "physics/physics_world.h"

namespace Ballance
{
void RegisterComponents(Engine::ComponentRegistry& componentRegistry)
{
    Engine::RegisterComponent<Component::MotionBlurMovementComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::AntiGravityTag>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::FloorTag>(componentRegistry, Engine::Origin::Game, false, false);

    Engine::RegisterComponent<Component::CheckpointComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::DeathZoneComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::PlayerSpawnComponent>(componentRegistry, Engine::Origin::Game, false, false);

    Engine::RegisterComponent<Component::BreakableComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::SelfDestructComponent>(componentRegistry, Engine::Origin::Game, false, false);
    Engine::RegisterComponent<Component::CrusherComponent>(componentRegistry, Engine::Origin::Game, false, false);
}

void RegisterCommands(Engine::EngineState* state)
{
    RegisterBreakableCommands(state);
}

void ConnectObservers(entt::registry& registry)
{
    ConnectCheckpointObservers(registry);
    ConnectBreakableObservers(registry);
}

void DisconnectObservers(entt::registry& registry)
{
    DisconnectCheckpointObservers(registry);
    DisconnectBreakableObservers(registry);
}

void CollectSystems(Engine::SystemGraph& graph)
{
    graph.Add("Ballance/AcquireBreakableAssets", &AcquireBreakableAssets);
    graph.Add("Ballance/UpdateSelfDestruct", &UpdateSelfDestruct, Engine::SystemWhen::WorldRunning);
    graph.Add("Ballance/UpdateCrushers", &UpdateCrushers, Engine::SystemWhen::WorldRunning);

    graph.Add("Ballance/DebugProcessPhysicsCollisions", &DebugProcessPhysicsCollisions, Engine::SystemWhen::Gameplay);
    graph.Add("Ballance/DebugApplyGroundForces", &DebugApplyGroundForces, Engine::SystemWhen::Gameplay);
    graph.Add("Ballance/CheckpointUpdate", &CheckpointUpdate, Engine::SystemWhen::Gameplay);
    graph.Add("Ballance/DeathZoneUpdate", &DeathZoneUpdate, Engine::SystemWhen::Gameplay);
    graph.Add("Ballance/PlayerControllerUpdate", [](Engine::EngineContext* ctx, Engine::EngineState* state) {
        PhysicsPlayerController& playerController = ctx->GetGameState<State>()->playerController;
        if (playerController.GetCharacter()) {
            playerController.Update(ctx, state);
        }
    }, Engine::SystemWhen::Gameplay);

    graph.Add("Ballance/DebugUpdate", &DebugUpdate);
}

void PrepareFrame(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
#ifdef WDEBUG
    DebugRender(ctx, state, frameBuffer);
#endif
}

void PlayStart(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    glm::vec3 spawnPosition{0.0f, 3.0f, 0.0f};
    int32_t bestPriority = INT32_MIN;
    auto spawnView = state->registry.view<Component::PlayerSpawnComponent, Component::TransformComponent>();
    for (auto entity : spawnView) {
        auto& spawn = spawnView.get<Component::PlayerSpawnComponent>(entity);
        if (spawn.priority > bestPriority) {
            bestPriority = spawn.priority;
            spawnPosition = spawnView.get<Component::TransformComponent>(entity).translation + spawn.offset;
        }
    }

    ctx->GetGameState<State>()->playerController.Initialize(state, ctx, spawnPosition);
}

void PlayStop(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    PhysicsPlayerController& playerController = ctx->GetGameState<State>()->playerController;
    if (playerController.GetCharacter()) {
        playerController.Shutdown(ctx->physicsWorld);
    }
}
} // Ballance
