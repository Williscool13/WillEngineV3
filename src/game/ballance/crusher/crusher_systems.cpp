//
// Created by William on 2026-10-22.
//

#include "crusher_systems.h"

#include <algorithm>
#include <initializer_list>

#include <glm/glm.hpp>
#include <tracy/Tracy.hpp>

#include "core/containers/arena_fixed_vector.h"
#include "core/time/time_frame.h"
#include "core/types/aabb.h"
#include "engine/engine_api.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_components.h"
#include "engine/include/engine_context.h"
#include "engine/systems/physics_system.h"
#include "game/ballance/fwd_components.h"
#include "game/ballance/breakable/breakable_component.h"
#include "game/ballance/breakable/breakable_systems.h"
#include "game/ballance/crusher/crusher_component.h"
#include "game/logging/game_log.h"
#include "physics/physics_system.h"

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
static Core::ArenaFixedVector<CrateHit> FindCrates(Engine::EngineContext* ctx, Engine::EngineState* state, const AABB& region, const Mat4& toLocal)
{
    const auto entities = Engine::QueryEntitiesInBox(ctx, state, region);
    auto crates = Core::ArenaFixedVector<CrateHit>(&ctx->gameplayArena.Get(), std::max(entities.Size(), size_t{1}));
    for (const entt::entity entity : entities) {
        const auto* body = Component::FindBody(state->registry, entity);
        if (!body || !state->registry.all_of<Component::BreakableComponent>(entity)) { continue; }
        crates.PushBack({entity, ctx->physicsSystem->GetWorldBounds(body->bodyID).Transformed(toLocal)});
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

static bool AnyCrateWhollyUnder(Engine::EngineContext* ctx, Engine::EngineState* state, const Mat4& toHead, const AABB& head, const AABB& headWorld, float depth)
{
    for (const CrateHit& crate : FindCrates(ctx, state, RegionBelow(headWorld, 0.0f, depth), toHead)) {
        if (crate.local.min.y < head.min.y && InsideXZ(crate.local, head)) { return true; }
    }
    return false;
}

/** Rule A: every crate under any part of the head breaks once the head is within `reach` of its top. */
static void BreakCratesUnderHead(Engine::EngineContext* ctx, Engine::EngineState* state, const Mat4& toHead, const AABB& head, const AABB& headWorld, float reach)
{
    for (const CrateHit& crate : FindCrates(ctx, state, RegionBelow(headWorld, 0.0f, reach), toHead)) {
        const bool bBelow = crate.local.min.y < head.min.y;
        if (bBelow && crate.local.max.y >= head.min.y - reach && OverlapsXZ(crate.local, head)) {
            Break(ctx, state, crate.entity);
        }
    }
}

/** Rule C: nothing intact leaves on the pusher. Any crate in what is left of its sweep breaks; one getting this far means the head or the belts let it through. */
static void BreakCratesInPusherPath(Engine::EngineContext* ctx, Engine::EngineState* state, entt::entity pusher, float remaining)
{
    entt::registry& registry = state->registry;
    const Mat4 toWorld = Component::ComputeWorldTransform(registry, pusher).GetMatrix();
    const Mat4 toLocal = glm::inverse(toWorld);

    const Physics::PhysicsSystem* physics = ctx->physicsSystem;
    AABB sweep;
    for (const auto& [entity, hierarchy, body] : registry.view<Component::HierarchyComponent, Component::PhysicsBodyComponent>().each()) {
        if (hierarchy.parent != pusher || body.bodyID.IsInvalid()) { continue; }
        sweep.Encapsulate(physics->GetWorldBounds(body.bodyID).Transformed(toLocal));
    }
    if (!sweep.IsValid()) { return; }
    // Only what is ahead of the leading face, not crates resting on the slope behind the pusher
    const float front = sweep.min.x;
    sweep.min.x = front - remaining;
    sweep.max.x = front + CONTACT_MARGIN;

    for (const CrateHit& crate : FindCrates(ctx, state, sweep.Transformed(toWorld), toLocal)) {
        if (!crate.local.Overlaps(sweep)) { continue; }
        const auto* name = registry.try_get<Component::NameComponent>(crate.entity);
        WLOG_WARN(Crusher, "Crate '{}' reached the pusher intact; broken there", name ? name->name.c_str() : "Unnamed");
        Break(ctx, state, crate.entity);
    }
}

static void SetBelts(Engine::EngineState* state, Component::CrusherComponent& crusher, bool bEnabled)
{
    for (Engine::EntityRef* ref : {&crusher.beltA, &crusher.beltB}) {
        const entt::entity belt = ref->Get(state);
        if (auto* conveyor = belt != entt::null ? state->registry.try_get<Component::ConveyorComponent>(belt) : nullptr) {
            conveyor->bEnabled = bEnabled;
        }
    }
}

static void Enter(Component::CrusherComponent& crusher, Component::CrusherPhase phase)
{
    crusher.phase = phase;
    crusher.phaseTime = 0.0f;
}

static void MoveAlong(entt::registry& registry, entt::entity entity, const Vec3& localDirection, float distance)
{
    if (entity == entt::null || distance == 0.0f) { return; }
    auto* transform = registry.try_get<Component::TransformComponent>(entity);
    if (!transform) { return; }
    transform->translation += transform->rotation * localDirection * distance;
    registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
}

/** The arm pivots at its top, so stretching it by the head's drop keeps it joined to the head. */
static void StretchArm(Engine::EngineContext* ctx, entt::registry& registry, entt::entity arm, float drop)
{
    const auto* armBody = Component::FindBody(registry, arm);
    auto* transform = armBody ? registry.try_get<Component::TransformComponent>(arm) : nullptr;
    if (!transform) { return; }
    const AABB bounds = ctx->physicsSystem->GetLocalBounds(armBody->bodyID);
    const float length = bounds.max.y - bounds.min.y;
    if (length <= 1e-4f) { return; }
    const float scale = 1.0f + drop / length;
    if (transform->scale.y == scale) { return; }
    transform->scale.y = scale;
    registry.emplace_or_replace<Component::DirtyTransformTag>(arm);
}

static void UpdateCrusher(Engine::EngineContext* ctx, Engine::EngineState* state, Component::CrusherComponent& crusher, float dt)
{
    entt::registry& registry = state->registry;
    const entt::entity head = crusher.head.Get(state);
    const auto* headBody = Component::FindBody(registry, head);
    if (!headBody) { return; }

    const Physics::PhysicsSystem* physics = ctx->physicsSystem;
    const Mat4 toHead = glm::inverse(physics->GetCenterOfMassTransform(headBody->bodyID));
    const AABB headLocal = physics->GetLocalBounds(headBody->bodyID);
    const AABB headWorld = physics->GetWorldBounds(headBody->bodyID);
    const entt::entity pusher = crusher.pusher.Get(state);

    const float previousDrop = crusher.drop;
    const float previousPushed = crusher.pushed;
    crusher.phaseTime += dt;

    switch (crusher.phase) {
        case Component::CrusherPhase::Waiting:
            if (crusher.phaseTime >= crusher.restTime && AnyCrateWhollyUnder(ctx, state, toHead, headLocal, headWorld, crusher.stroke + 1.0f)) {
                SetBelts(state, crusher, false);
                Enter(crusher, Component::CrusherPhase::Slam);
            }
            break;
        case Component::CrusherPhase::Slam:
            BreakCratesUnderHead(ctx, state, toHead, headLocal, headWorld, crusher.slamSpeed * dt * BREAK_LOOKAHEAD_FRAMES);
            crusher.drop = glm::min(crusher.stroke, crusher.drop + crusher.slamSpeed * dt);
            if (crusher.drop >= crusher.stroke) { Enter(crusher, Component::CrusherPhase::Hold); }
            break;
        case Component::CrusherPhase::Hold:
            BreakCratesUnderHead(ctx, state, toHead, headLocal, headWorld, CONTACT_MARGIN);
            if (crusher.phaseTime >= crusher.holdTime) { Enter(crusher, Component::CrusherPhase::Rise); }
            break;
        case Component::CrusherPhase::Rise:
            crusher.drop = glm::max(0.0f, crusher.drop - crusher.riseSpeed * dt);
            if (crusher.drop <= 0.0f) { Enter(crusher, pusher != entt::null ? Component::CrusherPhase::Push : Component::CrusherPhase::Return); }
            break;
        case Component::CrusherPhase::Push:
            BreakCratesInPusherPath(ctx, state, pusher, crusher.pushDistance - crusher.pushed);
            crusher.pushed = glm::min(crusher.pushDistance, crusher.pushed + crusher.pushSpeed * dt);
            if (crusher.pushed >= crusher.pushDistance) { Enter(crusher, Component::CrusherPhase::Return); }
            break;
        case Component::CrusherPhase::Return:
            crusher.pushed = glm::max(0.0f, crusher.pushed - crusher.returnSpeed * dt);
            if (crusher.pushed <= 0.0f) {
                SetBelts(state, crusher, true);
                Enter(crusher, Component::CrusherPhase::Waiting);
            }
            break;
    }

    MoveAlong(registry, head, Vec3(0.0f, -1.0f, 0.0f), crusher.drop - previousDrop);
    if (crusher.drop != previousDrop) { StretchArm(ctx, registry, crusher.arm.Get(state), crusher.drop); }
    MoveAlong(registry, pusher, Vec3(-1.0f, 0.0f, 0.0f), crusher.pushed - previousPushed);
}

void UpdateCrushers(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto view = state->registry.view<Component::CrusherComponent>();
    if (view.size() == 0) { return; }
    const float dt = state->timeFrame->deltaTime;
    for (const auto& [entity, crusher] : view.each()) {
        UpdateCrusher(ctx, state, crusher, dt);
    }
}
} // Ballance
