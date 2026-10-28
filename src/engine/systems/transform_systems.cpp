//
// Created by William on 2026-10-28.
//

#include "transform_systems.h"

#include <algorithm>

#include <tracy/Tracy.hpp>

#include "core/containers/arena_fixed_set.h"
#include "engine/engine_api.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_components.h"
#include "engine/include/engine_context.h"
#include "engine/systems/scene_system.h"

namespace Engine
{
static uint16_t HierarchyDepth(const SystemRegistry& registry, entt::entity entity)
{
    uint16_t depth = 0;
    const auto* node = registry.TryGet<Component::HierarchyComponent>(entity);
    while (node && node->parent != entt::null && registry.Valid(node->parent)) {
        node = registry.TryGet<Component::HierarchyComponent>(node->parent);
        if (++depth >= 1024) { break; }
    }
    return depth;
}

static void RefreshHierarchyOrder(SystemRegistry& registry)
{
    ZoneScoped;
    for (auto [entity, node] : registry.View<Mut<Component::HierarchyComponent>>().each()) {
        node.depth = HierarchyDepth(registry, entity);
    }
    registry.Sort<Component::HierarchyComponent>(
        [](const Component::HierarchyComponent& a, const Component::HierarchyComponent& b) { return a.depth < b.depth; },
        entt::insertion_sort{});
}

void EnsureHierarchyOrder(EngineState* state, SystemRegistry& registry)
{
    if (!state->bHierarchyOrderDirty) { return; }
    RefreshHierarchyOrder(registry);
    state->bHierarchyOrderDirty = false;
}

void SortHierarchy(EngineContext* ctx, EngineState* state, SystemRegistry& registry)
{
    EnsureHierarchyOrder(state, registry);
}

void PropagateDirtyTransforms(EngineContext* ctx, EngineState* state, SystemRegistry& registry)
{
    ZoneScoped;
    if (registry.View<Component::DirtyTransformRequest>().empty()) { return; }

    const bool bRunning = IsRunning(state);
    auto hierarchy = registry.View<Component::HierarchyComponent>();
    auto marked = Core::ArenaFixedSet<uint32_t>(&ctx->gameplayArena.Get(), std::max(hierarchy.size() * 2, size_t{1}));
    for (auto [entity, node] : hierarchy.each()) {
        if (!registry.Valid(node.parent)) { continue; }
        if (!registry.Has<Component::DirtyTransformRequest>(node.parent) && !marked.Contains(entt::to_integral(node.parent))) { continue; }
        if (bRunning && registry.Has<Component::DynamicPhysicsBodyRuntime>(entity)) { continue; }
        if (registry.Has<Component::DirtyTransformRequest>(entity) || !marked.Insert(entt::to_integral(entity))) { continue; }
        registry.Set<Component::DirtyTransformRequest>(entity);
    }
}

static void ApplyOrphan(SystemRegistry& registry, const entt::entity& entity)
{
    if (!registry.Valid(entity) || !registry.Has<Component::WorldTransformRuntime>(entity)) { return; }
    const auto& world = registry.Get<Component::WorldTransformRuntime>(entity);
    auto& local = registry.GetMut<Component::TransformComponent>(entity);
    local.translation = world.translation;
    local.rotation = world.rotation;
    local.scale = world.scale;
    registry.Remove<Component::HierarchyComponent>(entity);
}

/** Writes the body's simulated pose into world; true when it changed. */
static bool ApplySimulatedPose(const Component::DynamicPhysicsBodyRuntime& physics, const Component::TransformComponent& local, Component::WorldTransformRuntime& world)
{
    if (physics.currentPosition == world.translation && physics.currentRotation == world.rotation && local.scale == world.scale) { return false; }
    world.translation = physics.currentPosition;
    world.rotation = physics.currentRotation;
    world.scale = local.scale;
    return true;
}

/** Interpolation moves the render pose while the last step moved the body, and until the render pose has landed on the simulated one. */
static bool RenderInterpolates(const Component::DynamicPhysicsBodyRuntime& physics, const Component::WorldTransformRuntime& world, const Component::RenderWorldTransformRuntime& render)
{
    if (physics.previousPosition != physics.currentPosition || physics.previousRotation != physics.currentRotation) { return true; }
    return world.translation != render.translation || world.rotation != render.rotation || world.scale != render.scale;
}

void ResolveWorldTransforms(EngineContext* ctx, EngineState* state, SystemRegistry& registry)
{
    ZoneScoped;
    Core::Vector<entt::entity>& driven = state->physicsDrivenTransforms;
    driven.Clear();

    auto hierarchy = registry.View<Component::HierarchyComponent>();
    auto physicsRoots = registry.View<Component::DynamicPhysicsBodyRuntime, Component::TransformComponent, Mut<Component::WorldTransformRuntime>, Component::RenderWorldTransformRuntime>(
        entt::exclude<Component::HierarchyComponent>);
    const size_t capacity = std::max((hierarchy.size() + physicsRoots.size_hint()) * 2, size_t{1});
    // Changes from this pass are not applied yet, so these carry them down to the children
    auto moved = Core::ArenaFixedSet<uint32_t>(&ctx->gameplayArena.Get(), capacity);
    auto drivenSet = Core::ArenaFixedSet<uint32_t>(&ctx->gameplayArena.Get(), capacity);
    auto markDriven = [&](entt::entity entity) {
        driven.PushBack(entity);
        drivenSet.Insert(entt::to_integral(entity));
    };

    // Roots (no parent), non-physics
    for (auto [entity, local, world] : registry.View<Component::TransformComponent, Mut<Component::WorldTransformRuntime>, Component::DirtyTransformRequest>(
             entt::exclude<Component::HierarchyComponent, Component::DynamicPhysicsBodyRuntime>).each()) {
        world.translation = local.translation;
        world.rotation = local.rotation;
        world.scale = local.scale;
        registry.Set<Component::RenderTransformDirtyRequest>(entity);
    }

    // Roots, physics (simulated Jolt pose)
    for (auto [entity, physics, local, world, render] : physicsRoots.each()) {
        if (ApplySimulatedPose(physics, local, world)) { moved.Insert(entt::to_integral(entity)); }
        if (RenderInterpolates(physics, world, render)) { markDriven(entity); }
    }

    // Children in depth order
    auto isMoved = [&](entt::entity entity) { return registry.Has<Component::DirtyTransformRequest>(entity) || moved.Contains(entt::to_integral(entity)); };
    for (auto entity : hierarchy) {
        const auto& node = hierarchy.get<Component::HierarchyComponent>(entity);
        if (node.parent == entt::null || !registry.Valid(node.parent) || !registry.Has<Component::WorldTransformRuntime>(node.parent)) {
            registry.RunExclusive<&ApplyOrphan>(entity);
            continue;
        }

        // Physics (simulated Jolt pose)
        if (const auto* physics = registry.TryGet<Component::DynamicPhysicsBodyRuntime>(entity)) {
            auto& world = registry.GetMut<Component::WorldTransformRuntime>(entity);
            if (ApplySimulatedPose(*physics, registry.Get<Component::TransformComponent>(entity), world)) { moved.Insert(entt::to_integral(entity)); }
            if (RenderInterpolates(*physics, world, registry.Get<Component::RenderWorldTransformRuntime>(entity))) { markDriven(entity); }
            continue;
        }

        const bool bMoved = isMoved(entity) || isMoved(node.parent);
        if (bMoved) {
            registry.GetMut<Component::WorldTransformRuntime>(entity) = Component::ComposeWorldTransform(registry.Get<Component::WorldTransformRuntime>(node.parent), registry.Get<Component::TransformComponent>(entity));
            moved.Insert(entt::to_integral(entity));
        }
        if (drivenSet.Contains(entt::to_integral(node.parent))) {
            markDriven(entity);
        }
        else if (bMoved) {
            registry.Set<Component::RenderTransformDirtyRequest>(entity);
        }
    }
}
} // Engine
