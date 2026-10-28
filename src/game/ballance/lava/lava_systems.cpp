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
struct DissolveStart
{
    entt::entity entity;
    float sinkDepth;
    float duration;
    float tiltDegrees;
    float shrinkStart;
};

static void ApplyStartDissolving(Engine::SystemRegistry& registry, const DissolveStart& start)
{
    const entt::entity entity = start.entity;
    if (!registry.Valid(entity) || registry.Has<Component::DissolvingRuntime>(entity)) { return; }
    Engine::EngineState* state = registry.State();
    auto& transform = registry.GetMut<Component::TransformComponent>(entity);
    transform.mobility = Component::Mobility::Movable;

    glm::vec3 sink{0.0f, -start.sinkDepth, 0.0f};
    if (const auto* hierarchy = registry.TryGet<Component::HierarchyComponent>(entity); hierarchy && registry.Valid(hierarchy->parent)) {
        const Transform parent = Component::ComputeWorldTransform(registry, hierarchy->parent);
        sink = glm::inverse(parent.rotation) * sink / parent.scale;
    }

    glm::vec3 shrinkPivot{0.0f};
    if (const auto* body = Component::FindBody(registry, entity)) {
        const auto* ctx = registry.Context();
        const Transform world = Component::ComputeWorldTransform(registry, entity);
        shrinkPivot = glm::inverse(world.rotation) * (ctx->physicsWorld->GetWorldBounds(body->bodyID).Center() - world.translation) / world.scale;
    }

    glm::vec3 axis = state->rng.UnitVector();
    axis.y = 0.0f;
    const float axisLength = glm::length(axis);
    registry.Add<Component::DissolvingRuntime>(entity,Component::DissolvingRuntime{
        .startTranslation = transform.translation,
        .startRotation = transform.rotation,
        .startScale = transform.scale,
        .sink = sink,
        .tiltAxis = axisLength > 1e-4f ? glm::inverse(transform.rotation) * (axis / axisLength) : glm::vec3(1.0f, 0.0f, 0.0f),
        .shrinkPivot = shrinkPivot,
        .tiltRadians = glm::radians(start.tiltDegrees) * state->rng.Range(0.3f, 1.0f),
        .duration = start.duration,
        .shrinkStart = std::clamp(start.shrinkStart, 0.0f, 0.99f),
    });
    if (registry.Has<Component::PhysicsBodyDesc>(entity)) { Component::PhysicsBodyDesc::Suspend(registry, entity); }
}

void StartDissolving(entt::entity entity, float sinkDepth, float duration, float tiltDegrees, Engine::SystemRegistry& registry, float shrinkStart)
{
    registry.RunExclusive<&ApplyStartDissolving>(DissolveStart{.entity = entity, .sinkDepth = sinkDepth, .duration = duration, .tiltDegrees = tiltDegrees, .shrinkStart = shrinkStart});
}

void LavaContacts(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    if (registry.View<Component::LavaComponent>().size() == 0) { return; }

    entt::entity playerEntity = entt::null;
    if (const PhysicsCharacter* character = ctx->GetGameState<State>()->playerController.GetCharacter()) {
        playerEntity = character->GetEntity();
    }

    for (const auto& event : state->physics.resolvedAddedEvents) {
        for (const auto [lavaEntity, other] : {std::pair{event.e1, event.e2}, std::pair{event.e2, event.e1}}) {
            if (lavaEntity == entt::null || other == entt::null || other == playerEntity) { continue; }
            const auto* lava = registry.TryGet<Component::LavaComponent>(lavaEntity);
            if (!lava || registry.Has<Component::DissolvingRuntime>(other)) { continue; }
            const auto* desc = registry.TryGet<Component::PhysicsBodyDesc>(other);
            if (!desc || Component::EffectiveMotionType(registry, other, *desc) != Component::PhysicsMotionType::Dynamic) { continue; }
            const auto* body = Component::FindBody(registry, other);
            const auto* lavaBody = Component::FindBody(registry, lavaEntity);
            if (!body || !lavaBody) { continue; }
            // A cart's origin is its post top, above its body
            const float top = std::max(ctx->physicsWorld->GetWorldBounds(body->bodyID).max.y, registry.Get<Component::WorldTransformRuntime>(other).translation.y);
            const float surface = ctx->physicsWorld->GetWorldBounds(lavaBody->bodyID).max.y;
            const float depth = std::max(top - surface, 0.0f) + lava->extraDepth;
            StartDissolving(other, depth, std::clamp(depth / lava->sinkSpeed, lava->minDuration, std::max(lava->minDuration, lava->maxDuration)), lava->tiltDegrees, registry);
        }
    }
}

void UpdateDissolving(Engine::EngineContext* ctx, Engine::EngineState* state, Engine::SystemRegistry& registry)
{
    ZoneScoped;
    auto view = registry.View<Engine::Mut<Component::DissolvingRuntime>, Component::TransformComponent>();
    if (view.size_hint() == 0) { return; }

    const float dt = state->timeFrame->deltaTime;
    auto finished = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), view.size_hint());
    for (const auto& [entity, dissolving, current] : view.each()) {
        Component::TransformComponent transform = current;
        dissolving.elapsed += dt;
        const float u = std::min(dissolving.elapsed / dissolving.duration, 1.0f);
        const float sinkT = u * u * (3.0f - 2.0f * u);
        const float shrink = std::clamp((u - dissolving.shrinkStart) / (1.0f - dissolving.shrinkStart), 0.0f, 1.0f);
        transform.rotation = dissolving.startRotation * glm::angleAxis(dissolving.tiltRadians * sinkT, dissolving.tiltAxis);
        transform.scale = dissolving.startScale * (1.0f - shrink);
        transform.translation = dissolving.startTranslation + dissolving.sink * sinkT + transform.rotation * (dissolving.startScale * dissolving.shrinkPivot * shrink);
        registry.Set<Component::TransformComponent>(entity, transform);
        registry.Set<Component::DirtyTransformRequest>(entity);
        if (u >= 1.0f) { finished.PushBack(entity); }
    }
    if (finished.Size() == 0) { return; }

    auto destroyed = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), finished.Size());
    for (const entt::entity entity : finished) {
        registry.Remove<Component::DissolvingRuntime>(entity);
        if (registry.Has<Component::DissolveKeepTag>(entity)) {
            registry.Set<Component::DissolvedFlag>(entity);
            continue;
        }
        destroyed.PushBack(entity);
        registry.PushCommand({.type = Engine::CommandType::DestroyEntity, .entity = entity});
    }
    if (destroyed.Size() == 0) { return; }
    for (const auto& [child, hierarchy] : registry.View<Component::HierarchyComponent>().each()) {
        if (registry.Valid(hierarchy.parent) && destroyed.Contains(hierarchy.parent)) {
            registry.PushCommand({.type = Engine::CommandType::DestroyEntity, .entity = child});
        }
    }
}
} // Ballance
