//
// Created by William on 2026-10-27.
//

#include "culler_systems.h"

#include <algorithm>

#include <tracy/Tracy.hpp>

#include "core/containers/arena_fixed_vector.h"
#include "core/time/time_frame.h"
#include "engine/engine_api.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/include/engine_context.h"
#include "game/ballance/fwd_components.h"
#include "game/ballance/state.h"
#include "game/ballance/breakable/breakable_component.h"
#include "game/ballance/culler/culler_component.h"
#include "game/ballance/lava/lava_component.h"
#include "game/ballance/lava/lava_systems.h"
#include "game/logging/game_log.h"
#include "physics/physics_world.h"

namespace Ballance
{
struct Sleeper
{
    entt::entity entity;
    float since;
};

void UpdateCuller(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    auto cullers = registry.View<Component::CullerComponent>();
    if (cullers.size() == 0) { return; }
    const entt::entity cullerEntity = cullers.front();
    const auto& culler = registry.Get<Component::CullerComponent>(cullerEntity);
    const float now = state->timeFrame->totalTime - state->runStartTime;

    auto shards = registry.View<Component::ShardTag>(entt::exclude<Component::DissolvingRuntime>);
    auto sleepers = Core::ArenaFixedVector<Sleeper>(&ctx->gameplayArena.Get(), std::max(shards.size_hint(), size_t{1}));
    for (const entt::entity entity : shards) {
        const auto* body = Component::FindBody(registry, entity);
        if (body && !ctx->physicsWorld->IsAwake(body->bodyID)) {
            sleepers.PushBack({entity, registry.GetOrAdd<Component::ShardSleepRuntime>(entity, Component::ShardSleepRuntime{now}).sleepSince});
        }
        else {
            registry.Remove<Component::ShardSleepRuntime>(entity);
        }
    }
    TracyPlot("Ballance/Sleeping shards", static_cast<int64_t>(sleepers.Size()));

    const size_t budget = static_cast<size_t>(std::max(culler.sleepingShardBudget, 0));
    if (sleepers.Size() > budget) {
        const size_t excess = sleepers.Size() - budget;
        std::nth_element(sleepers.begin(), sleepers.begin() + excess, sleepers.end(), [](const Sleeper& a, const Sleeper& b) { return a.since < b.since; });
        for (size_t i = 0; i < excess; ++i) {
            StartDissolving(state, sleepers[i].entity, 0.0f, culler.cullSeconds, 0.0f, registry, 0.0f);
        }
        WLOG_DEBUG(Culler, "Culled {} sleeping shards ({} asleep, budget {})", excess, sleepers.Size(), budget);
    }

    entt::entity playerEntity = entt::null;
    if (const PhysicsCharacter* character = ctx->GetGameState<State>()->playerController.GetCharacter()) {
        playerEntity = character->GetEntity();
    }
    const Vec3 center = Component::ComputeWorldTransform(registry, cullerEntity).translation;
    const float radiusSq = culler.killRadius * culler.killRadius;
    auto bodies = registry.View<Component::PhysicsBodyDesc>(entt::exclude<Component::DissolvingRuntime>);
    auto lost = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), std::max(bodies.size_hint(), size_t{1}));
    for (const auto& [entity, desc] : bodies.each()) {
        if (entity == playerEntity || Component::EffectiveMotionType(registry, entity, desc) != Component::PhysicsMotionType::Dynamic) { continue; }
        const auto* body = Component::FindBody(registry, entity);
        if (!body) { continue; }
        const Vec3 p = ctx->physicsWorld->GetCenterOfMassPosition(body->bodyID);
        const float dx = p.x - center.x;
        const float dz = p.z - center.z;
        if (p.y < culler.killHeight || dx * dx + dz * dz > radiusSq) {
            const auto* name = registry.TryGet<Component::NameComponent>(entity);
            WLOG_WARN(Culler, "'{}' left the kill bounds at ({:.1f}, {:.1f}, {:.1f})", name ? name->name.c_str() : "Unnamed", p.x, p.y, p.z);
            lost.PushBack(entity);
        }
    }
    for (const entt::entity entity : lost) {
        StartDissolving(state, entity, 0.0f, culler.cullSeconds, 0.0f, registry, 0.0f);
    }
}
} // Ballance
