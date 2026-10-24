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

static void CaptureCarts(entt::registry& registry)
{
    auto view = registry.view<Component::CaneCartComponent, Component::TransformComponent, Component::PathMoverComponent, Component::PhysicsBodyDesc>(
        entt::exclude<Component::CaneCartRuntime>);
    for (const auto& [entity, link, transform, mover, desc] : view.each()) {
        registry.emplace<Component::CaneCartRuntime>(entity, Component::CaneCartRuntime{.park = {transform.translation, transform.rotation, transform.scale}});
        registry.emplace_or_replace<Component::DissolveKeepTag>(entity);
    }
}

static void ClipOn(entt::registry& registry, entt::entity cart, Component::CaneCartRuntime& runtime, float carry)
{
    registry.emplace_or_replace<Component::PathMoverRuntime>(cart, Component::PathMoverRuntime{.waitTimer = -carry});
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
    registry.remove<Component::PathMoverRuntime, Component::MovingKinematicFlag, Component::DirtyKinematicPhysicsTransformRequest>(cart);
    registry.emplace_or_replace<Component::PhysicsMotionOverrideRuntime>(cart, Component::PhysicsMotionOverrideRuntime{.motionType = Component::PhysicsMotionType::Dynamic});
    runtime.releaseVelocity = velocity;
    if (const auto* body = Component::FindBody(registry, cart)) {
        ctx->physicsWorld->MakeDynamic(body->bodyID, velocity);
        registry.emplace_or_replace<Component::DynamicPhysicsBodyRuntime>(cart, transform.translation, transform.rotation, transform.translation, transform.rotation);
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
    registry.remove<Component::DissolvedFlag, Component::PathMoverRuntime, Component::SetVelocityRequest, Component::PhysicsMotionOverrideRuntime>(cart);
    auto& transform = registry.get<Component::TransformComponent>(cart);
    transform.translation = runtime.park.translation;
    transform.rotation = runtime.park.rotation;
    transform.scale = runtime.park.scale;
    registry.emplace_or_replace<Component::DirtyTransformRequest>(cart);
    Component::PhysicsBodyDesc::Resume(registry, cart);
    runtime.phase = Component::CaneCartPhase::Idle;
}

void UpdateCaneLifts(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    entt::registry& registry = state->registry;
    if (registry.view<Component::CaneLiftComponent>().size() == 0) { return; }

    CaptureCarts(registry);
    const float dt = state->timeFrame->deltaTime;

    auto carts = registry.view<Component::CaneCartComponent, Component::CaneCartRuntime, Component::TransformComponent>();
    for (const auto& [cart, link, runtime, transform] : carts.each()) {
        if (registry.all_of<Component::DissolvedFlag>(cart)) {
            Recycle(registry, cart, runtime);
            continue;
        }
        const entt::entity liftEntity = link.lift.Get(state);
        const auto* lift = registry.valid(liftEntity) ? registry.try_get<Component::CaneLiftComponent>(liftEntity) : nullptr;
        if (!lift) { continue; }

        if (runtime.phase == Component::CaneCartPhase::Lifting) {
            const auto* mover = registry.try_get<Component::PathMoverComponent>(cart);
            const auto* run = registry.try_get<Component::PathMoverRuntime>(cart);
            if (mover && run && mover->spline.points.Size() >= 2 && run->currentSegment == static_cast<int32_t>(mover->spline.points.Size()) - 1) {
                Release(ctx, registry, cart, runtime, *lift);
            }
        }
        else if (runtime.phase == Component::CaneCartPhase::Falling) {
            runtime.fallTime += dt;
            if (!runtime.bVelocityApplied && registry.all_of<Component::DynamicPhysicsBodyRuntime>(cart)) {
                registry.emplace_or_replace<Component::SetVelocityRequest>(cart, Component::SetVelocityRequest{.linearVelocity = runtime.releaseVelocity});
                runtime.bVelocityApplied = true;
            }
            if (registry.all_of<Component::DissolvingRuntime>(cart)) {
                runtime.phase = Component::CaneCartPhase::Eaten;
            }
            else if (runtime.fallTime > lift->releaseTimeout || transform.translation.y < lift->killHeight) {
                StartDissolving(state, cart, LOST_SINK_DEPTH, LOST_DISSOLVE_SECONDS, LOST_TILT_DEGREES);
                runtime.phase = Component::CaneCartPhase::Eaten;
            }
        }
    }

    for (const auto& [liftEntity, lift] : registry.view<Component::CaneLiftComponent>().each()) {
        auto& liftRun = registry.get_or_emplace<Component::CaneLiftRuntime>(liftEntity, Component::CaneLiftRuntime{.timer = lift.startDelay});
        liftRun.timer -= dt;
        while (liftRun.timer <= 0.0f) {
            const float carry = -liftRun.timer;
            liftRun.timer += lift.interval;
            for (const auto& [cart, link, runtime, transform] : carts.each()) {
                if (runtime.phase != Component::CaneCartPhase::Idle || link.lift.Get(state) != liftEntity) { continue; }
                ClipOn(registry, cart, runtime, carry);
                break;
            }
        }
    }
}
} // Ballance
