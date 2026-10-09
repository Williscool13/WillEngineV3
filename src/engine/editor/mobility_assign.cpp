//
// Created by William on 2026-10-09.
//

#include "mobility_assign.h"

#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/components/camera_components.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/scene_components.h"
#include "engine/components/physics/physics_body_desc.h"
#include "engine/components/physics/physics_components.h"
#include "engine/components/render/light_components.h"
#include "engine/editor/editor_systems.h"
#include "engine/systems/render_systems.h"

namespace Engine
{
static bool MovesItself(const EngineState* state, entt::entity entity)
{
    const entt::registry& registry = state->registry;
    if (registry.all_of<Component::DynamicPhysicsBodyComponent>(entity)) { return true; }
    if (const auto* body = registry.try_get<Component::PhysicsBodyDesc>(entity); body && body->motionType != Component::PhysicsMotionType::Static) { return true; }
    for (const ComponentEntry& entry : state->componentRegistry.registry) {
        if (entry.bMovesEntity && entry.has(registry, entity)) { return true; }
    }
    return false;
}

static const char* EntityName(const entt::registry& registry, entt::entity entity)
{
    const auto* name = registry.try_get<Component::NameComponent>(entity);
    return name ? name->name.c_str() : "(unnamed)";
}

bool IsMovedAtRuntime(const EngineState* state, entt::entity entity)
{
    const entt::registry& registry = state->registry;
    for (entt::entity e = entity; e != entt::null && registry.valid(e);) {
        if (MovesItself(state, e)) { return true; }
        const auto* hierarchy = registry.try_get<Component::HierarchyComponent>(e);
        e = hierarchy ? hierarchy->parent : entt::null;
    }
    return false;
}

static void WarnIfBakedLightMoves(EngineState* state, entt::entity entity, Component::LightShadowMode mode, MobilityAssignResult& result)
{
    const auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
    if (mode != Component::LightShadowMode::Baked || !transform || transform->mobility != Component::Mobility::Movable) { return; }
    ++result.bakedLightsOnMovers;
    LOG_WARN(Engine, "Light {} bakes its shadows but is Movable; its map will go stale", EntityName(state->registry, entity));
}

MobilityAssignResult AssignMobility(EngineState* state)
{
    MobilityAssignResult result{};
    entt::registry& registry = state->registry;

    for (const auto [entity, transform] : registry.view<Component::TransformComponent>(entt::exclude<Component::CameraComponent>).each()) {
        if (transform.bMobilityLocked) {
            ++result.lockedCount;
        }
        else {
            const Component::Mobility mobility = IsMovedAtRuntime(state, entity) ? Component::Mobility::Movable : Component::Mobility::Static;
            if (transform.mobility != mobility) {
                transform.mobility = mobility;
                ++result.changedCount;
                EvaluateInstanceRenderState(state, entity);
                if (const auto* scene = registry.try_get<Component::SceneComponent>(entity)) {
                    MarkSceneModified(state, scene->sceneId);
                }
            }
        }
        if (transform.mobility == Component::Mobility::Static) { ++result.staticCount; }
        else { ++result.movableCount; }
    }

    for (const auto [entity, light] : registry.view<Component::AreaLightComponent>().each()) {
        WarnIfBakedLightMoves(state, entity, light.shadowMode, result);
    }
    for (const auto [entity, light] : registry.view<Component::SphereLightComponent>().each()) {
        WarnIfBakedLightMoves(state, entity, light.shadowMode, result);
    }
    return result;
}

void LockStaticTransforms(EngineState* state)
{
    entt::registry& registry = state->registry;
    for (const auto [entity, transform] : registry.view<Component::TransformComponent>(entt::exclude<Component::CameraComponent>).each()) {
        if (transform.mobility != Component::Mobility::Static || IsMovedAtRuntime(state, entity)) { continue; }
        registry.emplace_or_replace<Component::StaticTransformLockComponent>(entity, transform.translation, transform.rotation, transform.scale, false);
    }
}

void RejectStaticTransformWrites(EngineState* state)
{
    entt::registry& registry = state->registry;
    auto view = registry.view<Component::StaticTransformLockComponent, Component::TransformComponent, Component::DirtyTransformTag>();
    for (const auto [entity, lock, transform] : view.each()) {
        if (!lock.bWarned) {
            lock.bWarned = true;
            LOG_WARN(Engine, "{} is Static and cannot move during play; set it to Movable", EntityName(registry, entity));
        }
        transform.translation = lock.translation;
        transform.rotation = lock.rotation;
        transform.scale = lock.scale;
        registry.remove<Component::DirtyTransformTag>(entity);
    }
}
} // Engine
