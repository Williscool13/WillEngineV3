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
static bool MovesItself(const EngineState* state, entt::entity entity, const Engine::SystemRegistry& registry)
{
    if (registry.Has<Component::DynamicPhysicsBodyRuntime>(entity)) { return true; }
    if (const auto* body = registry.TryGet<Component::PhysicsBodyDesc>(entity); body && body->motionType != Component::PhysicsMotionType::Static) { return true; }
    for (const ComponentEntry& entry : state->componentRegistry.registry) {
        if (entry.bMovesEntity && entry.has(registry, entity)) { return true; }
    }
    return false;
}

static const char* EntityName(const Engine::SystemRegistry& registry, entt::entity entity)
{
    const auto* name = registry.TryGet<Component::NameComponent>(entity);
    return name ? name->name.c_str() : "(unnamed)";
}

bool IsMovedAtRuntime(const EngineState* state, entt::entity entity, SystemRegistry& registry)
{
    for (entt::entity e = entity; e != entt::null && registry.Valid(e);) {
        if (MovesItself(state, e, registry)) { return true; }
        const auto* hierarchy = registry.TryGet<Component::HierarchyComponent>(e);
        e = hierarchy ? hierarchy->parent : entt::null;
    }
    return false;
}

static void WarnIfBakedLightMoves(EngineState* state, entt::entity entity, Component::LightShadowMode mode, MobilityAssignResult& result, SystemRegistry& registry)
{
    const auto* transform = registry.TryGet<Component::TransformComponent>(entity);
    if (mode != Component::LightShadowMode::Baked || !transform || transform->mobility != Component::Mobility::Movable) { return; }
    ++result.bakedLightsOnMovers;
    LOG_WARN(Engine, "Light {} bakes its shadows but is Movable; its map will go stale", EntityName(registry, entity));
}

MobilityAssignResult AssignMobility(EngineState* state, SystemRegistry& registry)
{
    MobilityAssignResult result{};

    for (const auto [entity, transform] : registry.View<Engine::Mut<Component::TransformComponent>>(entt::exclude<Component::CameraRuntime>).each()) {
        if (transform.bMobilityLocked) {
            ++result.lockedCount;
        }
        else {
            const Component::Mobility mobility = IsMovedAtRuntime(state, entity, registry) ? Component::Mobility::Movable : Component::Mobility::Static;
            if (transform.mobility != mobility) {
                transform.mobility = mobility;
                ++result.changedCount;
                EvaluateInstanceRenderState(state, entity, registry);
                if (const auto* scene = registry.TryGet<Component::SceneRuntime>(entity)) {
                    MarkSceneModified(state, scene->sceneId);
                }
            }
        }
        if (transform.mobility == Component::Mobility::Static) { ++result.staticCount; }
        else { ++result.movableCount; }
    }

    for (const auto [entity, light] : registry.View<Component::AreaLightComponent>().each()) {
        WarnIfBakedLightMoves(state, entity, light.shadowMode, result, registry);
    }
    for (const auto [entity, light] : registry.View<Component::SphereLightComponent>().each()) {
        WarnIfBakedLightMoves(state, entity, light.shadowMode, result, registry);
    }
    return result;
}

void LockStaticTransforms(EngineState* state, SystemRegistry& registry)
{
    for (const auto [entity, transform] : registry.View<Component::TransformComponent>(entt::exclude<Component::CameraRuntime>).each()) {
        // A Static physics body never moves while running either, whatever the mobility says, so its collider cannot be left behind.
        const auto* body = registry.TryGet<Component::PhysicsBodyDesc>(entity);
        const bool bStaticBody = body != nullptr && body->motionType == Component::PhysicsMotionType::Static;
        if ((transform.mobility != Component::Mobility::Static && !bStaticBody) || IsMovedAtRuntime(state, entity, registry)) { continue; }
        registry.Set<Component::StaticTransformLockRuntime>(entity, transform.translation, transform.rotation, transform.scale, false);
    }
}

void RejectStaticTransformWrites(EngineState* state, SystemRegistry& registry)
{
    auto view = registry.View<Engine::Mut<Component::StaticTransformLockRuntime>, Engine::Mut<Component::TransformComponent>, Component::DirtyTransformRequest>();
    for (const auto [entity, lock, transform] : view.each()) {
        if (!lock.bWarned) {
            lock.bWarned = true;
            LOG_WARN(Engine, "{} is Static (mobility or physics body) and cannot move while running; make it Movable, and Kinematic if it has a body", EntityName(registry, entity));
        }
        transform.translation = lock.translation;
        transform.rotation = lock.rotation;
        transform.scale = lock.scale;
        registry.Remove<Component::DirtyTransformRequest>(entity);
    }
}
} // Engine
