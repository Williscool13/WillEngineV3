//
// Created by William on 2026-10-24.
//

#include "cane_lift_systems.h"

#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

#include "core/containers/arena_fixed_vector.h"
#include "core/time/time_frame.h"
#include "engine/engine_api.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_components.h"
#include "engine/include/engine_context.h"
#include "game/ballance/fwd_components.h"
#include "game/ballance/cane/cane_lift_component.h"
#include "game/ballance/lava/lava_component.h"
#include "game/ballance/lava/lava_systems.h"
#include "physics/physics_world.h"

namespace Ballance
{
static constexpr float LOST_SINK_DEPTH = 0.6f;
static constexpr float LOST_DISSOLVE_SECONDS = 1.5f;
static constexpr float LOST_TILT_DEGREES = 25.0f;

static bool CaptureCarts(Engine::EngineContext* ctx, entt::registry& registry)
{
    auto view = registry.view<Component::CaneCartComponent, Component::TransformComponent, Component::PathMoverComponent, Component::PhysicsBodyDesc>(
        entt::exclude<Component::CaneCartRuntime>);
    if (view.size_hint() == 0) { return false; }

    auto fresh = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (const entt::entity entity : view) { fresh.PushBack(entity); }
    for (const entt::entity entity : fresh) {
        const auto& transform = registry.get<Component::TransformComponent>(entity);
        registry.emplace<Component::CaneCartRuntime>(entity, Component::CaneCartRuntime{
            .mover = registry.get<Component::PathMoverComponent>(entity),
            .body = registry.get<Component::PhysicsBodyDesc>(entity),
            .park = {transform.translation, transform.rotation, transform.scale},
        });
        registry.emplace_or_replace<Component::DissolveKeepTag>(entity);
        registry.remove<Component::PathMoverComponent>(entity);
    }
    return fresh.Size() > 0;
}

/** Advances a Once mover by `seconds` as UpdatePathMovers would; false once it reaches its last point, where the cart is released. */
static bool AdvanceMover(Component::PathMoverComponent& mover, float seconds)
{
    const auto last = static_cast<int32_t>(mover.spline.points.Size()) - 1;
    while (seconds > 0.0f) {
        if (mover.bIsWaiting) {
            if (seconds < mover.waitTimer) {
                mover.waitTimer -= seconds;
                return true;
            }
            seconds -= mover.waitTimer;
            mover.waitTimer = 0.0f;
            mover.bIsWaiting = false;
            continue;
        }
        const int32_t target = mover.currentSegment + 1;
        const Component::PathPointSettings& settings = mover.pointSettings[target];
        const float remaining = (1.0f - mover.progress) / settings.speed;
        if (seconds < remaining) {
            mover.progress += seconds * settings.speed;
            return true;
        }
        seconds -= remaining;
        mover.currentSegment = target;
        mover.progress = 0.0f;
        if (target >= last) { return false; }
        mover.bIsWaiting = true;
        mover.waitTimer = settings.waitTime;
    }
    return true;
}

/** Puts the carts each lift would have clipped on before the session started onto their paths, so the lifts start full. */
static void PrefillLifts(Engine::EngineState* state, entt::registry& registry)
{
    auto carts = registry.view<Component::CaneCartComponent, Component::CaneCartRuntime, Component::TransformComponent>();
    for (const auto& [liftEntity, lift] : registry.view<Component::CaneLiftComponent>().each()) {
        float elapsed = lift.interval - lift.timer;
        for (const auto& [cart, link, runtime, transform] : carts.each()) {
            if (runtime.phase != Component::CaneCartPhase::Idle || link.lift.Get(state) != liftEntity || runtime.mover.spline.points.Size() < 2) { continue; }
            Component::PathMoverComponent mover = runtime.mover;
            mover.currentSegment = 0;
            mover.progress = 0.0f;
            mover.direction = 1;
            mover.bIsWaiting = false;
            mover.waitTimer = 0.0f;
            if (!AdvanceMover(mover, elapsed)) { break; }

            glm::vec3 startPos;
            glm::quat startRot;
            glm::vec3 pos;
            glm::quat rot;
            Component::EvaluatePath(mover.spline, mover.pointSettings, 0, 1, 0.0f, startPos, startRot);
            Component::EvaluatePath(mover.spline, mover.pointSettings, mover.currentSegment, mover.currentSegment + 1, mover.progress, pos, rot);
            const glm::quat anchor = runtime.park.rotation * glm::inverse(startRot);
            transform.translation = runtime.park.translation + anchor * (pos - startPos);
            transform.rotation = anchor * rot;
            registry.emplace_or_replace<Component::DirtyTransformTag>(cart);
            registry.emplace_or_replace<Component::PathMoverComponent>(cart, mover);
            Component::PhysicsBodyDesc::RequestRebuild(registry, cart);
            runtime.phase = Component::CaneCartPhase::Lifting;
            elapsed += lift.interval;
        }
    }
}

static void ClipOn(entt::registry& registry, entt::entity cart, Component::CaneCartRuntime& runtime, float carry)
{
    Component::PathMoverComponent mover = runtime.mover;
    mover.currentSegment = 0;
    mover.progress = 0.0f;
    mover.direction = 1;
    mover.bIsWaiting = false;
    mover.waitTimer = -carry;
    registry.emplace_or_replace<Component::PathMoverComponent>(cart, mover);
    runtime.phase = Component::CaneCartPhase::Lifting;
}

static void Release(Engine::EngineContext* ctx, entt::registry& registry, entt::entity cart, Component::CaneCartRuntime& runtime, const Component::CaneLiftComponent& lift)
{
    const auto& mover = registry.get<Component::PathMoverComponent>(cart);
    const auto& transform = registry.get<Component::TransformComponent>(cart);
    const size_t last = mover.spline.points.Size() - 1;
    const glm::quat anchor = transform.rotation * glm::inverse(mover.pointSettings[last].rotation);
    glm::vec3 velocity = anchor * ((mover.spline.points[last].pos - mover.spline.points[last - 1].pos) * mover.pointSettings[last].speed);
    const float speed = glm::length(velocity);
    if (speed > 1e-4f) { velocity += velocity / speed * lift.releaseBoost; }

    // Switched in place so the cart keeps moving the frame it lets go; a rebuild would hold it still until the new body exists
    registry.remove<Component::PathMoverComponent, Component::MovingKinematicTag, Component::DirtyKinematicPhysicsTransformTag>(cart);
    registry.get<Component::PhysicsBodyDesc>(cart).motionType = Component::PhysicsMotionType::Dynamic;
    runtime.releaseVelocity = velocity;
    if (const auto* body = Component::FindBody(registry, cart)) {
        ctx->physicsWorld->MakeDynamic(body->bodyID, velocity);
        registry.emplace_or_replace<Component::DynamicPhysicsBodyComponent>(cart, transform.translation, transform.rotation, transform.translation, transform.rotation);
        runtime.bVelocityApplied = true;
    }
    else {
        Component::PhysicsBodyDesc::RequestRebuild(registry, cart);
        runtime.bVelocityApplied = false;
    }
    runtime.fallTime = 0.0f;
    runtime.phase = Component::CaneCartPhase::Falling;
}

static void Recycle(entt::registry& registry, entt::entity cart, Component::CaneCartRuntime& runtime)
{
    registry.remove<Component::DissolvedTag, Component::PathMoverComponent, Component::SetVelocityTag>(cart);
    auto& transform = registry.get<Component::TransformComponent>(cart);
    transform.translation = runtime.park.translation;
    transform.rotation = runtime.park.rotation;
    transform.scale = runtime.park.scale;
    registry.emplace_or_replace<Component::DirtyTransformTag>(cart);
    if (registry.all_of<Component::PhysicsBodyDesc>(cart)) {
        registry.replace<Component::PhysicsBodyDesc>(cart, runtime.body);
        Component::PhysicsBodyDesc::RequestRebuild(registry, cart);
    }
    else {
        registry.emplace<Component::PhysicsBodyDesc>(cart, runtime.body);
    }
    runtime.phase = Component::CaneCartPhase::Idle;
}

void UpdateCaneLifts(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    entt::registry& registry = state->registry;
    if (registry.view<Component::CaneLiftComponent>().size() == 0) { return; }

    if (CaptureCarts(ctx, registry)) { PrefillLifts(state, registry); }
    const float dt = state->timeFrame->deltaTime;

    auto carts = registry.view<Component::CaneCartComponent, Component::CaneCartRuntime, Component::TransformComponent>();
    for (const auto& [cart, link, runtime, transform] : carts.each()) {
        if (registry.all_of<Component::DissolvedTag>(cart)) {
            Recycle(registry, cart, runtime);
            continue;
        }
        const entt::entity liftEntity = link.lift.Get(state);
        const auto* lift = registry.valid(liftEntity) ? registry.try_get<Component::CaneLiftComponent>(liftEntity) : nullptr;
        if (!lift) { continue; }

        if (runtime.phase == Component::CaneCartPhase::Lifting) {
            const auto* mover = registry.try_get<Component::PathMoverComponent>(cart);
            if (mover && mover->spline.points.Size() >= 2 && mover->currentSegment == static_cast<int32_t>(mover->spline.points.Size()) - 1) {
                Release(ctx, registry, cart, runtime, *lift);
            }
        }
        else if (runtime.phase == Component::CaneCartPhase::Falling) {
            runtime.fallTime += dt;
            if (!runtime.bVelocityApplied && registry.all_of<Component::DynamicPhysicsBodyComponent>(cart)) {
                registry.emplace_or_replace<Component::SetVelocityTag>(cart, Component::SetVelocityTag{.linearVelocity = runtime.releaseVelocity});
                runtime.bVelocityApplied = true;
            }
            if (registry.all_of<Component::DissolvingComponent>(cart)) {
                runtime.phase = Component::CaneCartPhase::Eaten;
            }
            else if (runtime.fallTime > lift->releaseTimeout || transform.translation.y < lift->killHeight) {
                StartDissolving(state, cart, LOST_SINK_DEPTH, LOST_DISSOLVE_SECONDS, LOST_TILT_DEGREES);
                runtime.phase = Component::CaneCartPhase::Eaten;
            }
        }
    }

    for (const auto& [liftEntity, lift] : registry.view<Component::CaneLiftComponent>().each()) {
        lift.timer -= dt;
        while (lift.timer <= 0.0f) {
            const float carry = -lift.timer;
            lift.timer += lift.interval;
            for (const auto& [cart, link, runtime, transform] : carts.each()) {
                if (runtime.phase != Component::CaneCartPhase::Idle || link.lift.Get(state) != liftEntity) { continue; }
                ClipOn(registry, cart, runtime, carry);
                break;
            }
        }
    }
}
} // Ballance
