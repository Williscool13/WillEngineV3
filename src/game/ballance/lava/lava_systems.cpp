//
// Created by William on 2026-10-24.
//

#include "lava_systems.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include <glm/gtc/quaternion.hpp>
#include <tracy/Tracy.hpp>

#include "core/containers/arena_fixed_vector.h"
#include "core/time/time_frame.h"
#include "engine/engine_api.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/include/engine_context.h"
#include "engine/systems/command_queue.h"
#include "game/ballance/fwd_components.h"
#include "game/ballance/state.h"
#include "game/ballance/lava/lava_component.h"
#include "physics/physics_world.h"

namespace Ballance
{
/** Fraction of the dissolve after which the body shrinks away; it is under the surface by then. */
static constexpr float SHRINK_START = 0.8f;

struct LavaDestroyedTag
{};

void StartDissolving(Engine::EngineState* state, entt::entity entity, float sinkDepth, float duration, float tiltDegrees)
{
    entt::registry& registry = state->registry;
    if (registry.all_of<Component::DissolvingComponent>(entity)) { return; }
    auto& transform = registry.get<Component::TransformComponent>(entity);
    transform.mobility = Component::Mobility::Movable;

    glm::vec3 sink{0.0f, -sinkDepth, 0.0f};
    if (const auto* hierarchy = registry.try_get<Component::HierarchyComponent>(entity); hierarchy && registry.valid(hierarchy->parent)) {
        const Transform parent = Component::ComputeWorldTransform(registry, hierarchy->parent);
        sink = glm::inverse(parent.rotation) * sink / parent.scale;
    }

    glm::vec3 shrinkPivot{0.0f};
    if (const auto* body = Component::FindBody(registry, entity)) {
        const auto* ctx = registry.ctx().get<Engine::EngineContext*>();
        const Transform world = Component::ComputeWorldTransform(registry, entity);
        shrinkPivot = glm::inverse(world.rotation) * (ctx->physicsWorld->GetWorldBounds(body->bodyID).Center() - world.translation) / world.scale;
    }

    glm::vec3 axis = state->rng.UnitVector();
    axis.y = 0.0f;
    const float axisLength = glm::length(axis);
    registry.emplace<Component::DissolvingComponent>(entity, Component::DissolvingComponent{
        .startTranslation = transform.translation,
        .startRotation = transform.rotation,
        .startScale = transform.scale,
        .sink = sink,
        .tiltAxis = axisLength > 1e-4f ? glm::inverse(transform.rotation) * (axis / axisLength) : glm::vec3(1.0f, 0.0f, 0.0f),
        .shrinkPivot = shrinkPivot,
        .tiltRadians = glm::radians(tiltDegrees) * state->rng.Range(0.3f, 1.0f),
        .duration = duration,
    });
    registry.remove<Component::PhysicsBodyDesc>(entity);
}

void LavaContacts(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    entt::registry& registry = state->registry;
    if (registry.view<Component::LavaComponent>().size() == 0) { return; }

    entt::entity playerEntity = entt::null;
    if (const PhysicsCharacter* character = ctx->GetGameState<State>()->playerController.GetCharacter()) {
        playerEntity = character->GetEntity();
    }

    for (const auto& event : state->physics.resolvedAddedEvents) {
        for (const auto [lavaEntity, other] : {std::pair{event.e1, event.e2}, std::pair{event.e2, event.e1}}) {
            if (lavaEntity == entt::null || other == entt::null || other == playerEntity) { continue; }
            const auto* lava = registry.try_get<Component::LavaComponent>(lavaEntity);
            if (!lava || registry.all_of<Component::DissolvingComponent>(other)) { continue; }
            const auto* desc = registry.try_get<Component::PhysicsBodyDesc>(other);
            if (!desc || desc->motionType != Component::PhysicsMotionType::Dynamic) { continue; }
            const auto* body = Component::FindBody(registry, other);
            const auto* lavaBody = Component::FindBody(registry, lavaEntity);
            if (!body || !lavaBody) { continue; }
            // A cart's origin is its post top, above its body
            const float top = std::max(ctx->physicsWorld->GetWorldBounds(body->bodyID).max.y, Component::ComputeWorldTransform(registry, other).translation.y);
            const float surface = ctx->physicsWorld->GetWorldBounds(lavaBody->bodyID).max.y;
            const float depth = std::max(top - surface, 0.0f) + lava->extraDepth;
            StartDissolving(state, other, depth, std::clamp(depth / lava->sinkSpeed, lava->minDuration, std::max(lava->minDuration, lava->maxDuration)), lava->tiltDegrees);
        }
    }
}

void UpdateDissolving(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    entt::registry& registry = state->registry;
    auto view = registry.view<Component::DissolvingComponent, Component::TransformComponent>();
    if (view.size_hint() == 0) { return; }

    const float dt = state->timeFrame->deltaTime;
    auto finished = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (const auto& [entity, dissolving, transform] : view.each()) {
        dissolving.elapsed += dt;
        const float u = std::min(dissolving.elapsed / dissolving.duration, 1.0f);
        const float sinkT = u * u * (3.0f - 2.0f * u);
        const float shrink = std::clamp((u - SHRINK_START) / (1.0f - SHRINK_START), 0.0f, 1.0f);
        transform.rotation = dissolving.startRotation * glm::angleAxis(dissolving.tiltRadians * sinkT, dissolving.tiltAxis);
        transform.scale = dissolving.startScale * (1.0f - shrink);
        transform.translation = dissolving.startTranslation + dissolving.sink * sinkT + transform.rotation * (dissolving.startScale * dissolving.shrinkPivot * shrink);
        registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
        if (u >= 1.0f) { finished.PushBack(entity); }
    }
    if (finished.Size() == 0) { return; }

    bool bDestroyed = false;
    for (const entt::entity entity : finished) {
        registry.remove<Component::DissolvingComponent>(entity);
        if (registry.all_of<Component::DissolveKeepTag>(entity)) {
            registry.emplace_or_replace<Component::DissolvedTag>(entity);
            continue;
        }
        registry.emplace<LavaDestroyedTag>(entity);
        state->commandQueue.Push({.type = Engine::CommandType::DestroyEntity, .entity = entity});
        bDestroyed = true;
    }
    if (!bDestroyed) { return; }
    for (const auto& [child, hierarchy] : registry.view<Component::HierarchyComponent>().each()) {
        if (registry.valid(hierarchy.parent) && registry.all_of<LavaDestroyedTag>(hierarchy.parent)) {
            state->commandQueue.Push({.type = Engine::CommandType::DestroyEntity, .entity = child});
        }
    }
}
} // Ballance
