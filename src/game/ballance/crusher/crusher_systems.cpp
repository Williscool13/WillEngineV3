//
// Created by William on 2026-10-22.
//

#include "crusher_systems.h"

#include <algorithm>
#include <initializer_list>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

#include "core/containers/arena_fixed_vector.h"
#include "core/time/time_frame.h"
#include "core/types/aabb.h"
#include "engine/engine_api.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_components.h"
#include "engine/console/console.h"
#include "engine/include/engine_context.h"
#include "engine/systems/physics_system.h"
#include "engine/systems/scene_system.h"
#include "game/ballance/fwd_components.h"
#include "game/ballance/breakable/breakable_component.h"
#include "game/ballance/breakable/breakable_systems.h"
#include "game/ballance/crusher/crusher_component.h"
#include "game/ballance/culler/culler_component.h"
#include "game/logging/game_log.h"
#include "physics/physics_world.h"

namespace Ballance
{
static constexpr float BREAK_LOOKAHEAD_FRAMES = 2.0f;
static constexpr float CONTACT_MARGIN = 0.05f;

static bool OverlapsXZ(const AABB& a, const AABB& b)
{
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.z < b.max.z && a.max.z > b.min.z;
}

static bool InsideXZ(const AABB& inner, const AABB& outer)
{
    return inner.min.x >= outer.min.x && inner.max.x <= outer.max.x && inner.min.z >= outer.min.z && inner.max.z <= outer.max.z;
}

struct CrateHit
{
    entt::entity entity;
    AABB local;
};

/** Intact crates whose bodies touch the world region, with their bounds in the frame `toLocal` maps into. */
static Core::ArenaFixedVector<CrateHit> FindCrates(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, const AABB& region, const Mat4& toLocal)
{
    const auto entities = Engine::QueryEntitiesInBox(ctx, state, registry, region);
    auto crates = Core::ArenaFixedVector<CrateHit>(&ctx->gameplayArena.Get(), std::max(entities.Size(), size_t{1}));
    for (const entt::entity entity : entities) {
        const auto* body = Component::FindBody(registry, entity);
        if (!body || !registry.Has<Component::BreakableComponent>(entity)) { continue; }
        crates.PushBack({entity, registry.Service<Physics::PhysicsWorld>().GetWorldBounds(body->bodyID).Transformed(toLocal)});
    }
    return crates;
}

/** The region under the head, from its bottom down to `depth` below, in world space. */
static AABB RegionBelow(const AABB& headWorld, float above, float depth)
{
    AABB region = headWorld;
    region.max.y = headWorld.min.y + above;
    region.min.y = headWorld.min.y - depth;
    return region;
}

static bool AnyCrateWhollyUnder(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, const Mat4& toHead, const AABB& head, const AABB& headWorld, float depth)
{
    for (const CrateHit& crate : FindCrates(ctx, state, registry, RegionBelow(headWorld, 0.0f, depth), toHead)) {
        if (crate.local.min.y < head.min.y && InsideXZ(crate.local, head)) { return true; }
    }
    return false;
}

/** Rule A: every crate under any part of the head breaks once the head is within `reach` of its top. */
static void BreakCratesUnderHead(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, const Mat4& toHead, const AABB& head, const AABB& headWorld, float reach)
{
    for (const CrateHit& crate : FindCrates(ctx, state, registry, RegionBelow(headWorld, 0.0f, reach), toHead)) {
        const bool bBelow = crate.local.min.y < head.min.y;
        if (bBelow && crate.local.max.y >= head.min.y - reach && OverlapsXZ(crate.local, head)) {
            Break(ctx, crate.entity, registry);
        }
    }
}

/** Rule C: nothing intact leaves on the pusher. Any crate in what is left of its sweep breaks; one getting this far means the head or the belts let it through. */
static void BreakCratesInPusherPath(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, entt::entity pusher, float remaining)
{
    const Mat4 toWorld = Component::GetMatrix(registry.Get<Component::WorldTransformRuntime>(pusher));
    const Mat4 toLocal = glm::inverse(toWorld);

    const Physics::PhysicsWorld* physics = &registry.Service<Physics::PhysicsWorld>();
    AABB sweep;
    for (const auto& [entity, hierarchy, body] : registry.View<Component::HierarchyComponent, Component::PhysicsBodyRuntime>().each()) {
        if (hierarchy.parent != pusher || !body.bodyID.IsValid()) { continue; }
        sweep.Encapsulate(physics->GetWorldBounds(body.bodyID).Transformed(toLocal));
    }
    if (!sweep.IsValid()) { return; }
    // Only what is ahead of the leading face, not crates resting on the slope behind the pusher
    const float front = sweep.min.x;
    sweep.min.x = front - remaining;
    sweep.max.x = front + CONTACT_MARGIN;

    for (const CrateHit& crate : FindCrates(ctx, state, registry, sweep.Transformed(toWorld), toLocal)) {
        if (!crate.local.Overlaps(sweep)) { continue; }
        const auto* name = registry.TryGet<Component::NameComponent>(crate.entity);
        WLOG_WARN(Crusher, "Crate '{}' reached the pusher intact; broken there", name ? name->name.c_str() : "Unnamed");
        Break(ctx, crate.entity, registry);
    }
}

static void SetBelts(Engine::EngineState* state, const Component::CrusherComponent& crusher, bool bRunning, Engine::SystemRegistry& registry)
{
    for (const Engine::EntityRef* ref : {&crusher.beltA, &crusher.beltB}) {
        const entt::entity belt = ref->Get(state, registry);
        if (belt == entt::null || !registry.Has<Component::ConveyorComponent>(belt)) { continue; }
        if (bRunning) {
            registry.Remove<Component::ConveyorHaltedFlag>(belt);
        }
        else {
            registry.Set<Component::ConveyorHaltedFlag>(belt);
        }
    }
}

static void Enter(Component::CrusherRuntime& run, Component::CrusherPhase phase)
{
    run.phase = phase;
    run.phaseTime = 0.0f;
}

static void MoveAlong(Engine::SystemRegistry& registry, entt::entity entity, const Vec3& localDirection, float distance)
{
    if (entity == entt::null || distance == 0.0f) { return; }
    const auto* current = registry.TryGet<Component::TransformComponent>(entity);
    if (!current) { return; }
    Component::TransformComponent transform = *current;
    transform.translation += transform.rotation * localDirection * distance;
    registry.Set<Component::TransformComponent>(entity, transform);
    registry.Set<Component::DirtyTransformRequest>(entity);
}

/** The arm pivots at its top, so stretching it by the head's drop keeps it joined to the head. */
static void StretchArm(Engine::EngineContext* ctx, Engine::SystemRegistry& registry, entt::entity arm, float drop)
{
    const auto* armBody = Component::FindBody(registry, arm);
    const auto* current = armBody ? registry.TryGet<Component::TransformComponent>(arm) : nullptr;
    if (!current) { return; }
    const AABB bounds = registry.Service<Physics::PhysicsWorld>().GetLocalBounds(armBody->bodyID);
    const float length = bounds.max.y - bounds.min.y;
    if (length <= 1e-4f) { return; }
    const float scale = 1.0f + drop / length;
    if (current->scale.y == scale) { return; }
    Component::TransformComponent transform = *current;
    transform.scale.y = scale;
    registry.Set<Component::TransformComponent>(arm, transform);
    registry.Set<Component::DirtyTransformRequest>(arm);
}

static void UpdateCrusher(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry, const Component::CrusherComponent& crusher, Component::CrusherRuntime& run, float dt, bool bThroatClosed)
{
    const entt::entity head = crusher.head.Get(state, registry);
    const auto* headBody = Component::FindBody(registry, head);
    if (!headBody) { return; }

    const Physics::PhysicsWorld* physics = &registry.Service<Physics::PhysicsWorld>();
    const Mat4 toHead = glm::inverse(physics->GetCenterOfMassTransform(headBody->bodyID));
    const AABB headLocal = physics->GetLocalBounds(headBody->bodyID);
    const AABB headWorld = physics->GetWorldBounds(headBody->bodyID);
    const entt::entity pusher = crusher.pusher.Get(state, registry);

    const float previousDrop = run.drop;
    const float previousPushed = run.pushed;
    run.phaseTime += dt;

    switch (run.phase) {
        case Component::CrusherPhase::Waiting:
            SetBelts(state, crusher, !bThroatClosed, registry);
            if (bThroatClosed) { break; }
            if (run.phaseTime >= crusher.restTime && AnyCrateWhollyUnder(ctx, state, registry, toHead, headLocal, headWorld, crusher.stroke + 1.0f)) {
                SetBelts(state, crusher, false, registry);
                Enter(run, Component::CrusherPhase::Slam);
            }
            break;
        case Component::CrusherPhase::Slam:
            BreakCratesUnderHead(ctx, state, registry, toHead, headLocal, headWorld, crusher.slamSpeed * dt * BREAK_LOOKAHEAD_FRAMES);
            run.drop = glm::min(crusher.stroke, run.drop + crusher.slamSpeed * dt);
            if (run.drop >= crusher.stroke) { Enter(run, Component::CrusherPhase::Hold); }
            break;
        case Component::CrusherPhase::Hold:
            BreakCratesUnderHead(ctx, state, registry, toHead, headLocal, headWorld, CONTACT_MARGIN);
            if (run.phaseTime >= crusher.holdTime) { Enter(run, Component::CrusherPhase::Rise); }
            break;
        case Component::CrusherPhase::Rise:
            run.drop = glm::max(0.0f, run.drop - crusher.riseSpeed * dt);
            if (run.drop <= 0.0f) { Enter(run, pusher != entt::null ? Component::CrusherPhase::Push : Component::CrusherPhase::Return); }
            break;
        case Component::CrusherPhase::Push:
            BreakCratesInPusherPath(ctx, state, registry, pusher, crusher.pushDistance - run.pushed);
            run.pushed = glm::min(crusher.pushDistance, run.pushed + crusher.pushSpeed * dt);
            if (run.pushed >= crusher.pushDistance) { Enter(run, Component::CrusherPhase::Return); }
            break;
        case Component::CrusherPhase::Return:
            run.pushed = glm::max(0.0f, run.pushed - crusher.returnSpeed * dt);
            if (run.pushed <= 0.0f) {
                SetBelts(state, crusher, true, registry);
                Enter(run, Component::CrusherPhase::Waiting);
            }
            break;
    }

    MoveAlong(registry, head, Vec3(0.0f, -1.0f, 0.0f), run.drop - previousDrop);
    if (run.drop != previousDrop) { StretchArm(ctx, registry, crusher.arm.Get(state, registry), run.drop); }
    MoveAlong(registry, pusher, Vec3(-1.0f, 0.0f, 0.0f), run.pushed - previousPushed);
}

static int32_t CountLiveShards(const Engine::SystemRegistry& registry)
{
    return static_cast<int32_t>(registry.View<Component::ShardTag>().size());
}

/** Sleeping shards cost next to nothing, and ones resting out of every pusher's reach would otherwise hold the valve shut. */
static int32_t CountAwakeShards(Engine::EngineContext* ctx, const Engine::SystemRegistry& registry)
{
    int32_t awake = 0;
    for (const entt::entity entity : registry.View<Component::ShardTag>()) {
        const auto* body = Component::FindBody(registry, entity);
        if (body && registry.Service<Physics::PhysicsWorld>().IsAwake(body->bodyID)) { ++awake; }
    }
    return awake;
}

static bool AnyThroatClosed(const Engine::SystemRegistry& registry)
{
    for (const auto& [entity, run] : registry.View<Component::ThroatValveRuntime>().each()) {
        if (run.bClosed) { return true; }
    }
    return false;
}

void UpdateThroatValves(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    const int32_t shards = CountAwakeShards(ctx, registry);
    TracyPlot("Ballance/Live shards", static_cast<int64_t>(CountLiveShards(registry)));
    TracyPlot("Ballance/Awake shards", static_cast<int64_t>(shards));
    TracyPlot("Ballance/Awake bodies", static_cast<int64_t>(registry.Service<Physics::PhysicsWorld>().GetAwakeBodyCount()));

    const float dt = state->timeFrame->deltaTime;
    for (const auto& [entity, valve] : registry.View<Component::ThroatValveComponent>().each()) {
        Component::ThroatValveRuntime* existing = registry.TryGetMut<Component::ThroatValveRuntime>(entity);
        Component::ThroatValveRuntime added{};
        Component::ThroatValveRuntime& run = existing ? *existing : added;
        if (!run.bClosed) { run.openTime += dt; }
        const bool bClose = !run.bClosed && run.openTime >= valve.minOpenSeconds && shards > valve.closeShards;
        const bool bOpen = run.bClosed && shards <= valve.openShards;
        if (bClose || bOpen) {
            run.bClosed = bClose;
            run.openTime = 0.0f;
            WLOG_INFO(Crusher, "Throat valve {} at {} awake shards", bClose ? "closed" : "opened", shards);
        }
        if (!existing) { registry.Set<Component::ThroatValveRuntime>(entity, added); }
    }
}

struct CrateSpawn
{
    StringID prefabId;
    Vec3 position;
    glm::quat rotation;
};

static void ApplySpawnCrate(Engine::SystemRegistry& registry, const CrateSpawn& spawn)
{
    const entt::entity crate = Engine::SpawnPrefab(registry.Context(), registry.State(), registry, spawn.prefabId, spawn.position);
    if (crate == entt::null) { return; }
    registry.Set<Component::DoNotSerializeTag>(crate);
    auto& transform = registry.GetMut<Component::TransformComponent>(crate);
    transform.rotation = spawn.rotation;
    transform.mobility = Component::Mobility::Movable;
}

void UpdateCrateFeeders(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    auto view = registry.View<Component::CrateFeederComponent>();
    if (view.size() == 0) { return; }

    const float dt = state->timeFrame->deltaTime;
    const bool bThroatClosed = AnyThroatClosed(registry);
    auto intact = static_cast<int32_t>(registry.View<Component::BreakableComponent>().size());
    for (const auto& [entity, feeder] : view.each()) {
        Component::CrateFeederRuntime* existing = registry.TryGetMut<Component::CrateFeederRuntime>(entity);
        Component::CrateFeederRuntime added{};
        Component::CrateFeederRuntime& run = existing ? *existing : added;
        if (!existing) { registry.Set<Component::CrateFeederRuntime>(entity, added); }
        if (bThroatClosed || intact >= feeder.maxIntact || feeder.cratesPerSecond <= 0.0f) {
            run.timer = 0.0f;
            continue;
        }
        run.timer += dt;
        const float interval = 1.0f / feeder.cratesPerSecond;
        if (run.timer < interval) { continue; }
        run.timer = glm::min(run.timer - interval, interval);

        const float angle = state->rng.Range(0.0f, glm::two_pi<float>());
        const float radius = glm::sqrt(state->rng.Range(feeder.minRadius * feeder.minRadius, feeder.maxRadius * feeder.maxRadius));
        const Vec3 position = registry.Get<Component::WorldTransformRuntime>(entity).translation + Vec3(glm::cos(angle) * radius, 0.0f, glm::sin(angle) * radius);
        const glm::quat rotation = glm::angleAxis(state->rng.Range(0.0f, glm::two_pi<float>()), state->rng.UnitVector());
        registry.RunExclusive<&ApplySpawnCrate>(CrateSpawn{.prefabId = feeder.prefabId, .position = position, .rotation = rotation});
        ++intact;
    }
}

void UpdateCrushers(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    auto view = registry.View<Component::CrusherComponent>();
    if (view.size() == 0) { return; }
    const float dt = state->timeFrame->deltaTime;
    const bool bThroatClosed = AnyThroatClosed(registry);
    for (const auto& [entity, crusher] : view.each()) {
        Component::CrusherRuntime* existing = registry.TryGetMut<Component::CrusherRuntime>(entity);
        Component::CrusherRuntime added{};
        UpdateCrusher(ctx, state, registry, crusher, existing ? *existing : added, dt, bThroatClosed);
        if (!existing) { registry.Set<Component::CrusherRuntime>(entity, added); }
    }
}

static void PrintFlow(Engine::EngineContext* ctx, Engine::EngineState* state, Core::Span<const char*>)
{
    Engine::SystemRegistry registry{state->registry, Engine::AccessSet::EXCLUSIVE, nullptr, "Console/b.flow"};
    const auto line = Core::InlineString<256>::Format("  live shards %d (%d awake, %zu stamped asleep), intact breakables %zu, awake bodies %u, throat %s", CountLiveShards(registry), CountAwakeShards(ctx, registry),
        registry.View<Component::ShardSleepRuntime>().size(), registry.View<Component::BreakableComponent>().size(), registry.Service<Physics::PhysicsWorld>().GetAwakeBodyCount(), AnyThroatClosed(registry) ? "closed" : "open");
    Engine::Console::Print(state, line.c_str());
}

void RegisterCrusherCommands(Engine::EngineState* state)
{
    Engine::Console::Register(state, Engine::Origin::Game, "b.flow", "Prints live shards, intact breakables, awake bodies and the throat valve state", &PrintFlow);
}
} // Ballance
