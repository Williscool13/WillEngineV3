//
// Created by William on 2025-12-25.
//

#include "physics_system.h"
#include "physics_integration.h"
#include "physics/physics_helpers.h"
#include "physics/physics_layers.h"
#include "physics/jolt_world.h"
#include "physics/physics_world.h"

#include <tracy/Tracy.hpp>

#include "core/containers/arena_fixed_vector.h"
#include "engine/include/engine_context.h"
#include "core/time/time_frame.h"
#include "engine/components/fwd_components.h"
#include "engine/components/common_components.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "engine/components/physics/physics_body_component.h"
#include "engine/components/physics/physics_shape_runtime.h"
#include "engine/components/render_components.h"
#include "engine/systems/scene_system.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h"
#include "Jolt/Physics/Collision/ObjectLayer.h"
#include "Jolt/Physics/Collision/TransformedShape.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"
#include "Jolt/Physics/Collision/Shape/ConvexHullShape.h"
#include "Jolt/Physics/Collision/Shape/MeshShape.h"
#include "Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h"
#include "Jolt/Physics/Collision/Shape/CylinderShape.h"
#include "engine/resources/physics/physics_collider_asset.h"
#include "engine/resources/physics/collider_generation.h"

namespace Engine
{
using Physics::PhysicsHelpers::ToJolt;
using Physics::PhysicsHelpers::ToHandle;

void ConnectPhysicsObservers(entt::registry& registry)
{
    registry.on_construct<Component::PhysicsBodyDesc>().connect<&Component::PhysicsBodyDesc::OnConstruct>();
    registry.on_destroy<Component::PhysicsBodyDesc>().connect<&Component::PhysicsBodyDesc::OnDestroy>();

    registry.on_destroy<Component::PhysicsBodyComponent>().connect<&Component::PhysicsBodyComponent::OnDestroy>();
}

void DisconnectPhysicsObservers(entt::registry& registry)
{
    registry.on_construct<Component::PhysicsBodyDesc>().disconnect<&Component::PhysicsBodyDesc::OnConstruct>();
    registry.on_destroy<Component::PhysicsBodyDesc>().disconnect<&Component::PhysicsBodyDesc::OnDestroy>();

    registry.on_destroy<Component::PhysicsBodyComponent>().disconnect<&Component::PhysicsBodyComponent::OnDestroy>();
}

void WakeBodiesAround(JPH::BodyInterface& bodyInterface, const JPH::BodyID& bodyId)
{
    if (bodyId.IsInvalid()) { return; }
    JPH::AABox bounds = bodyInterface.GetTransformedShape(bodyId).GetWorldSpaceBounds();
    if (!bounds.IsValid()) { return; }
    bounds.ExpandBy(JPH::Vec3::sReplicate(0.05f));
    bodyInterface.ActivateBodiesInAABox(bounds, JPH::BroadPhaseLayerFilter{}, JPH::ObjectLayerFilter{});
}

void PhysicsPreUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    if (!IsPhysicsStepping(state)) { return; }
    if (state->physics.bEnabled) {
        PhysicsUpdate(ctx, state);
    }
    ResolveCollisionEvents(ctx, state);
}

static void UpdateConveyors(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    auto& bodyInterface = ctx->physicsWorld->Jolt().GetBodyInterface();
    auto& listener = ctx->physicsWorld->Jolt().GetContactListener();
    uint32_t count = 0;
    for (const auto& [entity, conveyor, physicsBody, world] : state->registry.view<Component::ConveyorComponent, Component::PhysicsBodyComponent, Component::WorldTransformComponent>().each()) {
        if (!conveyor.bEnabled || !physicsBody.bodyID.IsValid()) { continue; }
        if (count >= Physics::MAX_CONVEYORS) {
            static bool bWarned = false;
            if (!bWarned) {
                LOG_WARN(Physics, "More than {} conveyors; the rest stand still", Physics::MAX_CONVEYORS);
                bWarned = true;
            }
            break;
        }

        Physics::ConveyorSurface surface{.body = ToJolt(physicsBody.bodyID)};
        if (conveyor.mode == Component::ConveyorComponent::Mode::Linear) {
            const glm::vec3 direction = world.rotation * conveyor.direction;
            const float length = glm::length(direction);
            if (length > 1e-6f) {
                const glm::vec3 velocity = direction * (conveyor.speed / length);
                surface.linearVelocity = JPH::Vec3(velocity.x, velocity.y, velocity.z);
            }
        }
        else {
            const glm::vec3 axis = world.rotation * conveyor.axis;
            const float length = glm::length(axis);
            if (length > 1e-6f) {
                const glm::vec3 omega = axis * (conveyor.speed / (length * glm::max(conveyor.referenceRadius, 0.01f)));
                surface.angularVelocity = JPH::Vec3(omega.x, omega.y, omega.z);
                surface.centre = JPH::RVec3(world.translation.x, world.translation.y, world.translation.z);
            }
        }

        if (listener.SetConveyor(count, surface)) {
            WakeBodiesAround(bodyInterface, surface.body);
        }
        ++count;
    }
    listener.SetConveyorCount(count);
}

void PhysicsUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto* physics = ctx->physicsWorld;
    state->physics.deltaTimeAccumulator += state->timeFrame->deltaTime * state->physics.timeScale;
    const uint32_t maxSteps = state->physics.timeScale > 1.0f ? Physics::MAX_TIMESCALE_STEPS_PER_FRAME : Physics::MAX_PHYSICS_STEPS_PER_FRAME;
    state->physics.deltaTimeAccumulator = std::min(state->physics.deltaTimeAccumulator, static_cast<float>(maxSteps) * Physics::PHYSICS_TIMESTEP);
    const uint32_t stepCount = static_cast<uint32_t>(state->physics.deltaTimeAccumulator / Physics::PHYSICS_TIMESTEP);

    UpdateConveyors(ctx, state);
    if (stepCount == 0) {
        state->physics.interpolationAlpha = state->physics.deltaTimeAccumulator / Physics::PHYSICS_TIMESTEP;
        return;
    }

    auto& bodyInterface = physics->Jolt().GetBodyInterface();

    // Movers advance once per frame; each step moves its kinematic body a 1/stepCount slice of the way, so it arrives on the last step at its real speed.
    struct KinematicMove
    {
        JPH::BodyID bodyID;
        glm::vec3 startPosition;
        glm::quat startRotation;
        Transform target;
    };
    auto stoppedView = state->registry.view<Component::PhysicsBodyComponent, Component::MovingKinematicTag>(entt::exclude<Component::DirtyKinematicPhysicsTransformTag>);
    if (stoppedView.size_hint() > 0) {
        auto stopped = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), stoppedView.size_hint());
        for (const auto& [entity, physicsBody] : stoppedView.each()) {
            if (physicsBody.bodyID.IsValid()) { bodyInterface.SetLinearAndAngularVelocity(ToJolt(physicsBody.bodyID), JPH::Vec3::sZero(), JPH::Vec3::sZero()); }
            stopped.PushBack(entity);
        }
        for (const entt::entity entity : stopped) { state->registry.remove<Component::MovingKinematicTag>(entity); }
    }

    auto kinematicView = state->registry.view<Component::PhysicsBodyComponent, Component::TransformComponent, Component::DirtyKinematicPhysicsTransformTag>();
    auto kinematicMoves = Core::ArenaFixedVector<KinematicMove>(&ctx->gameplayArena.Get(), std::max(kinematicView.size_hint(), size_t{1}));
    for (const auto& [entity, physicsBody, transform] : kinematicView.each()) {
        state->registry.emplace_or_replace<Component::MovingKinematicTag>(entity);
        const JPH::BodyID bodyID = ToJolt(physicsBody.bodyID);
        const JPH::RVec3 p = bodyInterface.GetPosition(bodyID);
        const JPH::Quat r = bodyInterface.GetRotation(bodyID);
        kinematicMoves.PushBack({
            .bodyID = bodyID,
            .startPosition = {static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())},
            .startRotation = {r.GetW(), r.GetX(), r.GetY(), r.GetZ()},
            .target = Component::ComputeWorldTransform(state->registry, entity),
        });
    }
    state->registry.clear<Component::DirtyKinematicPhysicsTransformTag>();

    // todo: Truly teleport mechanics are a little more user-defined. Need a unified "mark movement as teleport" function that emplaces the teleport physics transform tag to allow kinematics to teleport.
    for (uint32_t step = 0; step < stepCount; ++step) {
        // Teleport
        auto teleportView = state->registry.view<Component::PhysicsBodyComponent, Component::TransformComponent, Component::TeleportPhysicsTransformTag>();
        for (const auto& [entity, physicsBody, transform] : teleportView.each()) {
            const Transform world = Component::ComputeWorldTransform(state->registry, entity);
            bodyInterface.SetPositionAndRotation(
                ToJolt(physicsBody.bodyID),
                JPH::RVec3(world.translation.x, world.translation.y, world.translation.z),
                JPH::Quat(world.rotation.x, world.rotation.y, world.rotation.z, world.rotation.w),
                JPH::EActivation::Activate
            );
            if (auto* dynamic = state->registry.try_get<Component::DynamicPhysicsBodyComponent>(entity)) {
                dynamic->previousPosition = dynamic->currentPosition = world.translation;
                dynamic->previousRotation = dynamic->currentRotation = world.rotation;
            }
        }
        state->registry.clear<Component::TeleportPhysicsTransformTag>();

        // Kinematic
        const float t = static_cast<float>(step + 1) / static_cast<float>(stepCount);
        for (const KinematicMove& move : kinematicMoves) {
            const glm::vec3 position = glm::mix(move.startPosition, move.target.translation, t);
            const glm::quat rotation = glm::slerp(move.startRotation, move.target.rotation, t);
            bodyInterface.MoveKinematic(move.bodyID, JPH::RVec3(position.x, position.y, position.z), JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), Physics::PHYSICS_TIMESTEP);
        }

        // Set velocity
        auto velocityView = state->registry.view<Component::PhysicsBodyComponent, Component::SetVelocityTag>();
        if (velocityView.size_hint() > 0) {
            auto applied = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), velocityView.size_hint());
            for (const auto& [entity, physicsBody, tag] : velocityView.each()) {
                bodyInterface.SetLinearAndAngularVelocity(
                    ToJolt(physicsBody.bodyID),
                    JPH::Vec3(tag.linearVelocity.x, tag.linearVelocity.y, tag.linearVelocity.z),
                    JPH::Vec3(tag.angularVelocity.x, tag.angularVelocity.y, tag.angularVelocity.z)
                );
                applied.PushBack(entity);
            }
            for (const entt::entity entity : applied) { state->registry.remove<Component::SetVelocityTag>(entity); }
        }

        auto saveView = state->registry.view<Component::DynamicPhysicsBodyComponent>();
        for (auto [entity, dynamic] : saveView.each()) {
            dynamic.previousPosition = dynamic.currentPosition;
            dynamic.previousRotation = dynamic.currentRotation;
        }

        physics->Step(Physics::PHYSICS_TIMESTEP);

        auto dynamicView = state->registry.view<Component::DynamicPhysicsBodyComponent, Component::PhysicsBodyComponent, Component::TransformComponent>();
        for (auto [entity, dynamic, physicsBody, transform] : dynamicView.each()) {
            JPH::RVec3 pos = bodyInterface.GetPosition(ToJolt(physicsBody.bodyID));
            JPH::Quat rot = bodyInterface.GetRotation(ToJolt(physicsBody.bodyID));

            const glm::vec3 worldPosition(pos.GetX(), pos.GetY(), pos.GetZ());
            const glm::quat worldRotation(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
            dynamic.currentPosition = worldPosition;
            dynamic.currentRotation = worldRotation;

            // Back-fill the local transform (T/R; physics never scales) so it stays the body's parent-relative pose.
            if (const auto* node = state->registry.try_get<Component::HierarchyComponent>(entity); node && state->registry.valid(node->parent)) {
                const auto& parentWorld = state->registry.get<Component::WorldTransformComponent>(node->parent);
                const glm::quat invParentRotation = glm::inverse(parentWorld.rotation);
                transform.translation = (invParentRotation * (worldPosition - parentWorld.translation)) / parentWorld.scale;
                transform.rotation = glm::normalize(invParentRotation * worldRotation);
            }
            else {
                transform.translation = worldPosition;
                transform.rotation = worldRotation;
            }
        }

        state->physics.deltaTimeAccumulator -= Physics::PHYSICS_TIMESTEP;
        ++state->physics.stepCount;
    }

    state->physics.interpolationAlpha = state->physics.deltaTimeAccumulator / Physics::PHYSICS_TIMESTEP;
}

Core::ArenaFixedVector<entt::entity> QueryEntitiesInBox(Engine::EngineContext* ctx, const Engine::EngineState* state, const AABB& box)
{
    Core::Arena* arena = &ctx->gameplayArena.Get();
    const auto bodies = ctx->physicsWorld->QueryBodiesInBox(arena, box);
    auto entities = Core::ArenaFixedVector<entt::entity>(arena, std::max(bodies.Size(), size_t{1}));
    for (const Physics::BodyHandle body : bodies) {
        const entt::entity entity = Component::EntityFromBodyUserData(state->registry, ctx->physicsWorld->GetUserData(body));
        if (entity != entt::null) { entities.PushBack(entity); }
    }
    return entities;
}

void ResolveCollisionEvents(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    const entt::registry& registry = state->registry;
    Physics::JoltWorld& world = ctx->physicsWorld->Jolt();
    state->physics.resolvedAddedEvents.Clear();
    for (const auto& event : world.GetAddedEvents()) {
        state->physics.resolvedAddedEvents.PushBack({
            Component::EntityFromBodyUserData(registry, event.userData1),
            Component::EntityFromBodyUserData(registry, event.userData2),
            {event.worldNormal.GetX(), event.worldNormal.GetY(), event.worldNormal.GetZ()},
            {event.contactPoint.GetX(), event.contactPoint.GetY(), event.contactPoint.GetZ()},
            event.penetrationDepth
        });
    }

    state->physics.resolvedPersistedEvents.Clear();
    for (const auto& event : world.GetPersistedEvents()) {
        state->physics.resolvedPersistedEvents.PushBack({
            Component::EntityFromBodyUserData(registry, event.userData1),
            Component::EntityFromBodyUserData(registry, event.userData2),
            {event.worldNormal.GetX(), event.worldNormal.GetY(), event.worldNormal.GetZ()},
            {event.contactPoint.GetX(), event.contactPoint.GetY(), event.contactPoint.GetZ()},
            event.penetrationDepth
        });
    }

    state->physics.resolvedRemovedEvents.Clear();
    for (const auto& event : world.GetRemovedEvents()) {
        state->physics.resolvedRemovedEvents.PushBack({
            Component::EntityFromBodyUserData(registry, world.GetBodyInterface().GetUserData(event.body1)),
            Component::EntityFromBodyUserData(registry, world.GetBodyInterface().GetUserData(event.body2)),
        });
    }
}

void MarkPhysicsTransformsDirty(Engine::EngineState* state)
{
    auto& registry = state->registry;
    auto view = registry.view<Component::PhysicsBodyComponent, Component::DirtyTransformTag>();
    for (auto entity : view) {
        const auto* desc = registry.try_get<Component::PhysicsBodyDesc>(entity);
        const auto* node = registry.try_get<Component::HierarchyComponent>(entity);
        const bool bMovedByParent = node && registry.valid(node->parent) && registry.all_of<Component::DirtyTransformTag>(node->parent);
        if (bMovedByParent && (!desc || desc->motionType == Component::PhysicsMotionType::Static)) {
            if (!registry.all_of<Component::StaticFollowWarnedTag>(entity)) {
                const auto* name = registry.try_get<Component::NameComponent>(entity);
                LOG_WARN(Physics, "Static body '{}' has a moving parent and stays put; make it kinematic", name ? name->name.c_str() : "Unnamed");
                registry.emplace<Component::StaticFollowWarnedTag>(entity);
            }
            continue;
        }
        if (desc && desc->motionType == Component::PhysicsMotionType::Kinematic) {
            state->registry.emplace_or_replace<Component::DirtyKinematicPhysicsTransformTag>(entity);
        }
        else {
            state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(entity);
            // An editor drag during Simulate holds the body still; without this gravity builds velocity it releases on drop.
            if (IsSimulating(state) && desc && desc->motionType == Component::PhysicsMotionType::Dynamic) {
                state->registry.emplace_or_replace<Component::SetVelocityTag>(entity);
            }
        }
    }
}

void UpdatePhysicsEditor(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto& bodyInterface = ctx->physicsWorld->Jolt().GetBodyInterface();
    auto view = state->registry.view<Component::PhysicsBodyComponent, Component::TransformComponent, Component::DirtyTransformTag>();
    for (const auto& [entity, physicsBody, transform] : view.each()) {
        const Transform world = Component::ComputeWorldTransform(state->registry, entity);
        bodyInterface.SetPositionAndRotation(
            ToJolt(physicsBody.bodyID),
            JPH::RVec3(world.translation.x, world.translation.y, world.translation.z),
            JPH::Quat(world.rotation.x, world.rotation.y, world.rotation.z, world.rotation.w),
            JPH::EActivation::DontActivate);
        if (auto* dynamic = state->registry.try_get<Component::DynamicPhysicsBodyComponent>(entity)) {
            dynamic->previousPosition = dynamic->currentPosition = world.translation;
            dynamic->previousRotation = dynamic->currentRotation = world.rotation;
        }
    }
}

void DebugRenderPhysics(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
#ifdef WDEBUG
    const uint32_t mask = state->projectConfig.editorPhysicsDebugMask;
    const bool bCasterChanges = state->debug.localShadow.bDrawCasterChangeBodies;
    if (mask == 0 && !bCasterChanges) { return; }

    auto& filter = ctx->physicsWorld->Jolt().GetDebugDrawFilter();
    filter.Clear();

    if (bCasterChanges) {
        for (const auto& [entity, runtime, dirty, physicsBody] : state->registry.view<Component::MeshRuntime, Component::MultiframeDirtyComponent, Component::PhysicsBodyComponent>().each()) {
            filter.AddBody(ToJolt(physicsBody.bodyID));
        }
    }

    const bool bAllBodies = (mask & Engine::PHYSICS_DEBUG_ALL_BODIES) != 0;
    ctx->physicsWorld->Jolt().SetDebugDrawForceLowestLOD(bAllBodies);

    if (bAllBodies) {
        for (const auto& [entity, physicsBody] : state->registry.view<Component::PhysicsBodyComponent>().each()) {
            filter.AddBody(ToJolt(physicsBody.bodyID));
        }
    }
    else {
        if (mask & Engine::PHYSICS_DEBUG_SENSORS) {
            for (const auto& [entity, physicsBody, bodyDesc] : state->registry.view<Component::PhysicsBodyComponent, Component::PhysicsBodyDesc>().each()) {
                if (bodyDesc.bIsSensor) { filter.AddBody(ToJolt(physicsBody.bodyID)); }
            }
        }
        if (mask & Engine::PHYSICS_DEBUG_TAGGED) {
            for (const auto& [entity, physicsBody] : state->registry.view<Component::DrawPhysicsDebugTag, Component::PhysicsBodyComponent>().each()) {
                filter.AddBody(ToJolt(physicsBody.bodyID));
            }
        }
        if (mask & Engine::PHYSICS_DEBUG_SELECTED) {
            for (entt::entity entity : state->editor.selectedEntities) {
                if (auto* physicsBody = state->registry.try_get<Component::PhysicsBodyComponent>(entity)) {
                    filter.AddBody(ToJolt(physicsBody->bodyID));
                }
            }
        }
        filter.SetDrawAwake((mask & Engine::PHYSICS_DEBUG_AWAKE) != 0);
    }

    ctx->physicsWorld->DrawDebug(&frameBuffer->mainViewFamily, true, (mask & Engine::PHYSICS_DEBUG_SLEEP_COLOR) != 0);
#endif
}


JPH::BodyID CreateBodyFromShape(JPH::BodyInterface& bodyInterface, const Component::PhysicsBodyDesc& desc, const JPH::ShapeRefC& shapeRef, JPH::RVec3 position, JPH::Quat rotation,
                                uint64_t userData, uint16_t layerOverride)
{
    JPH::EMotionType motionType = desc.motionType == Component::PhysicsMotionType::Static
                                      ? JPH::EMotionType::Static
                                      : desc.motionType == Component::PhysicsMotionType::Dynamic
                                            ? JPH::EMotionType::Dynamic
                                            : JPH::EMotionType::Kinematic;

    JPH::ObjectLayer layer;
    if (layerOverride != Component::NO_LAYER_OVERRIDE) {
        layer = layerOverride;
    }
    else if (desc.bIsSensor) {
        layer = Physics::Layers::SENSOR;
    }
    else {
        layer = desc.motionType == Component::PhysicsMotionType::Static ? Physics::Layers::NON_MOVING : Physics::Layers::MOVING;
    }

    JPH::BodyCreationSettings settings(shapeRef, position, rotation.Normalized(), motionType, layer);
    if (desc.motionType != Component::PhysicsMotionType::Static) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    settings.mFriction = desc.friction;
    settings.mRestitution = desc.restitution;
    settings.mMotionQuality = static_cast<JPH::EMotionQuality>(desc.motionQuality);
    settings.mEnhancedInternalEdgeRemoval = desc.bEnhancedInternalEdgeRemoval;
    settings.mIsSensor = desc.bIsSensor;
    settings.mUserData = userData;
    settings.mAllowDynamicOrKinematic = desc.motionType != Component::PhysicsMotionType::Static;
    if (desc.bLockTilt && desc.motionType != Component::PhysicsMotionType::Static) {
        settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ | JPH::EAllowedDOFs::RotationY;
    }

    JPH::EActivation activation = (desc.motionType == Component::PhysicsMotionType::Static && !desc.bIsSensor)
                                      ? JPH::EActivation::DontActivate
                                      : JPH::EActivation::Activate;
    return bodyInterface.CreateAndAddBody(settings, activation);
}

static JPH::ShapeRefC CreateShapeFromCollider(const Engine::PhysicsColliderAsset& collider, const glm::vec3& sc)
{
    switch (collider.kind) {
        case Engine::PhysicsColliderKind::Compound:
        {
            const auto& prims = collider.primitives;
            if (prims.Size() == 0) { return nullptr; }

            auto makeSub = [&](const Engine::SplineColliderPrimitive& prim) -> JPH::ShapeRefC {
                switch (prim.type) {
                    case Engine::SplineColliderPrimitiveType::Capsule:
                    {
                        const float radius = prim.radius * glm::max(sc.x, sc.z);
                        const float halfHeight = prim.halfHeight * sc.y;
                        JPH::CapsuleShapeSettings s(glm::max(0.001f, halfHeight), glm::max(0.001f, radius));
                        auto r = s.Create();
                        return r.HasError() ? JPH::ShapeRefC{} : r.Get();
                    }
                    case Engine::SplineColliderPrimitiveType::Sphere:
                    {
                        const float radius = prim.radius * glm::max(sc.x, glm::max(sc.y, sc.z));
                        JPH::SphereShapeSettings s(glm::max(0.001f, radius));
                        auto r = s.Create();
                        return r.HasError() ? JPH::ShapeRefC{} : r.Get();
                    }
                    case Engine::SplineColliderPrimitiveType::Cylinder:
                    {
                        const float radius = prim.radius * glm::max(sc.x, sc.z);
                        const float halfHeight = prim.halfHeight * sc.y;
                        JPH::CylinderShapeSettings s(glm::max(0.001f, halfHeight), glm::max(0.001f, radius));
                        auto r = s.Create();
                        return r.HasError() ? JPH::ShapeRefC{} : r.Get();
                    }
                    case Engine::SplineColliderPrimitiveType::ConvexHull:
                    {
                        // Vertices are absolute in the collider's local space (child transform is identity).
                        JPH::Array<JPH::Vec3> pts;
                        pts.reserve(prim.hullCount);
                        for (uint32_t i = 0; i < prim.hullCount; ++i) {
                            const glm::vec3 v = collider.positions[prim.hullOffset + i] * sc;
                            pts.push_back({v.x, v.y, v.z});
                        }
                        // Small convex radius: tread slabs are thin, so the default (0.05m) would exceed the half-thickness and fail Create.
                        JPH::ConvexHullShapeSettings s(pts, 0.001f);
                        auto r = s.Create();
                        if (r.HasError()) {
                            LOG_WARN(Engine, "Convex hull sub-shape ({} points) failed: {}", prim.hullCount, r.GetError().c_str());
                            return JPH::ShapeRefC{};
                        }
                        return r.Get();
                    }
                    default:
                        break;
                }
                const glm::vec3 he = prim.halfExtents * sc;
                const float minHE = glm::min(he.x, glm::min(he.y, he.z));
                const float convexRadius = glm::min(0.05f, glm::max(0.0f, minHE * 0.9f));
                JPH::BoxShapeSettings s(JPH::Vec3(he.x, he.y, he.z), convexRadius);
                auto r = s.Create();
                return r.HasError() ? JPH::ShapeRefC{} : r.Get();
            };

            if (prims.Size() == 1) {
                JPH::ShapeRefC sub = makeSub(prims[0]);
                if (!sub) { return nullptr; }
                const glm::vec3 pos = prims[0].position * sc;
                JPH::RotatedTranslatedShapeSettings rt(
                    JPH::Vec3(pos.x, pos.y, pos.z),
                    JPH::Quat(prims[0].rotation.x, prims[0].rotation.y, prims[0].rotation.z, prims[0].rotation.w),
                    sub);
                auto r = rt.Create();
                return r.HasError() ? nullptr : r.Get();
            }

            JPH::StaticCompoundShapeSettings compound;
            for (const auto& prim : prims) {
                JPH::ShapeRefC sub = makeSub(prim);
                if (!sub) { return nullptr; }
                const glm::vec3 pos = prim.position * sc;
                compound.AddShape(
                    JPH::Vec3(pos.x, pos.y, pos.z),
                    JPH::Quat(prim.rotation.x, prim.rotation.y, prim.rotation.z, prim.rotation.w),
                    sub);
            }
            auto result = compound.Create();
            if (result.HasError()) {
                LOG_WARN(Engine, "Compound shape creation failed: {}", result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }
        case Engine::PhysicsColliderKind::ConvexHull:
        {
            JPH::Array<JPH::Vec3> pts;
            pts.reserve(collider.positions.Size());
            for (const auto& p : collider.positions) {
                const glm::vec3 sp = p * sc;
                pts.push_back({sp.x, sp.y, sp.z});
            }
            JPH::ConvexHullShapeSettings s(pts);
            auto result = s.Create();
            if (result.HasError()) {
                LOG_WARN(Engine, "ConvexHull collider creation failed: {}", result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }
        case Engine::PhysicsColliderKind::TriangleMesh:
        {
            const auto& pos = collider.positions;
            const auto& idx = collider.indices;
            JPH::TriangleList tris;
            tris.reserve(idx.Size() / 3);
            for (size_t i = 0; i + 2 < idx.Size(); i += 3) {
                const glm::vec3 a = pos[idx[i]] * sc, b = pos[idx[i + 1]] * sc, c = pos[idx[i + 2]] * sc;
                tris.push_back(JPH::Triangle(
                    JPH::Float3(a.x, a.y, a.z),
                    JPH::Float3(b.x, b.y, b.z),
                    JPH::Float3(c.x, c.y, c.z)));
            }
            JPH::MeshShapeSettings s(tris);
            auto result = s.Create();
            if (result.HasError()) {
                LOG_WARN(Engine, "TriangleMesh collider creation failed: {}", result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }
    }
    return nullptr;
}

JPH::ShapeRefC CreateShapeFromDesc(const Component::PhysicsShapeDesc& desc, Engine::PhysicsColliderHandle colliderHandle, Engine::AssetManager* assetManager)
{
    if (colliderHandle.IsValid() && assetManager) {
        auto* collider = assetManager->GetCollider(colliderHandle);
        if (!collider || collider->loadState != Engine::PhysicsColliderAsset::LoadState::Loaded) { return nullptr; }
        return CreateShapeFromCollider(*collider, desc.bakedScale);
    }

    auto create = [](const JPH::ShapeSettings& s) {
        auto r = s.Create();
        return r.HasError() ? JPH::ShapeRefC{} : r.Get();
    };
    if (const auto* box = std::get_if<Component::BoxShape>(&desc.geometry)) {
        const glm::vec3 he = box->halfExtents;
        const float convexRadius = glm::min(JPH::cDefaultConvexRadius, glm::min(he.x, glm::min(he.y, he.z)) * 0.9f);
        return create(JPH::BoxShapeSettings(JPH::Vec3(he.x, he.y, he.z), convexRadius));
    }
    if (const auto* sphere = std::get_if<Component::SphereShape>(&desc.geometry)) {
        return create(JPH::SphereShapeSettings(sphere->radius));
    }
    if (const auto* capsule = std::get_if<Component::CapsuleShape>(&desc.geometry)) {
        return create(JPH::CapsuleShapeSettings(capsule->halfHeight, capsule->radius));
    }
    // A Collider resolves through its colliderHandle; without a valid one there is nothing to build.
    return nullptr;
}

void PhysicsMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PhysicsShapeRuntime, Component::PendingPhysicsMeshTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    auto abandoned = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, bodyDesc, runtime] : view.each()) {
        if (started.Size() + abandoned.Size() >= budget) { break; }
        bool allArmed = true;
        bool shouldAbandon = false;

        for (size_t i = 0; i < bodyDesc.shapes.Size(); ++i) {
            const auto* collider = std::get_if<Component::ColliderShape>(&bodyDesc.shapes[i].geometry);
            if (!collider) { continue; }
            Engine::PhysicsColliderHandle& handle = runtime.colliders[i];
            if (handle.IsValid()) { continue; }

            if (collider->meshSourceModelId.IsValid()) {
                if (ctx->assetManager->IsModelFrozen(collider->meshSourceModelId)) { allArmed = false; break; }

                const bool bWantsMesh = collider->bMeshPrecise && bodyDesc.motionType != Component::PhysicsMotionType::Dynamic;
                const Engine::PhysicsColliderKind kind = bWantsMesh ? Engine::PhysicsColliderKind::TriangleMesh : Engine::PhysicsColliderKind::ConvexHull;
                handle = ctx->assetManager->LoadModelCollider(collider->meshSourceModelId, kind, collider->meshSourceNodeIndex);
            }
            else if (!std::holds_alternative<std::monostate>(collider->proceduralParams)) {
                handle = ctx->assetManager->LoadProceduralCollider(collider->proceduralParams, collider->proceduralRepeat);
            }
            else if (!collider->splineParams.spline.points.IsEmpty()) {
                const bool bHollow = collider->bSplineHollow && bodyDesc.motionType != Component::PhysicsMotionType::Dynamic;
                handle = ctx->assetManager->LoadSplineCollider(collider->splineParams, bHollow);
            }
            else if (collider->text3DSource.IsValid()) {
                const Component::Text3DShapeSource& t = collider->text3DSource;
                if (ctx->assetManager->IsFontFrozen(t.fontId)) { allArmed = false; break; }
                handle = ctx->assetManager->LoadText3DCollider(t.fontId, t.text, t.depth, t.flatness, t.tracking, t.scale, t.bSmoothNormals, t.align, t.anchor, t.wrapWidth, t.bendRadius, t.bPrecise);
            }

            if (!handle.IsValid()) {
                LOG_WARN(Engine, "Physics collider source could not be loaded. Removing pending tag.");
                shouldAbandon = true;
                break;
            }
        }

        if (shouldAbandon) {
            abandoned.PushBack(entity);
            continue;
        }
        if (!allArmed) {
            continue;
        }
        started.PushBack(entity);
    }

    for (const entt::entity entity : abandoned) {
        state->registry.remove<Component::PendingPhysicsMeshTag>(entity);
    }
    for (const entt::entity entity : started) {
        state->registry.remove<Component::PendingPhysicsMeshTag>(entity);
        state->registry.emplace_or_replace<Component::PhysicsMeshLoadingTag>(entity);
    }
}

void PhysicsMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto view = state->registry.view<Component::PhysicsShapeRuntime, Component::PhysicsMeshLoadingTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);
    for (auto [entity, runtime] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        bool allReady = true;
        bool shouldAbandon = false;

        for (const Engine::PhysicsColliderHandle handle : runtime.colliders) {
            if (!handle.IsValid()) { continue; }

            auto* collider = ctx->assetManager->GetCollider(handle);
            if (!collider) {
                LOG_WARN(Engine, "Physics collider not found. Removing loading tag.");
                shouldAbandon = true;
                break;
            }
            if (collider->loadState == Engine::PhysicsColliderAsset::LoadState::FailedToLoad) {
                LOG_WARN(Engine, "Physics collider failed to load. Removing loading tag.");
                shouldAbandon = true;
                break;
            }
            if (collider->loadState != Engine::PhysicsColliderAsset::LoadState::Loaded) {
                allReady = false;
                break;
            }
        }

        if (!allReady && !shouldAbandon) {
            continue;
        }

        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::PhysicsMeshLoadingTag>(entity);
    }
}

void PhysicsShapeCreationResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;

    // Mesh based physics need to wait for its mesh to load
    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PhysicsShapeRuntime, Component::PendingPhysicsShapeCreationTag>(
        entt::exclude<Component::PendingPhysicsMeshTag, Component::PhysicsMeshLoadingTag>);

    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);

    for (auto [entity, bodyDesc, runtime] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        bool bDegenerate = false;
        for (const auto& shape : bodyDesc.shapes) {
            const auto* collider = std::get_if<Component::ColliderShape>(&shape.geometry);
            if (collider && !collider->HasSource()) {
                LOG_WARN(Engine, "PhysicsBodyDesc has a Collider shape with no source, skipping shape creation");
                bDegenerate = true;
                break;
            }
        }
        if (bDegenerate) {
            resolved.PushBack(entity);
            continue;
        }

        if (bodyDesc.shapes.IsEmpty()) {
            LOG_WARN(Engine, "PhysicsBodyDesc has no shapes, skipping shape creation");
            resolved.PushBack(entity);
            continue;
        }

        JPH::ShapeRefC shape;
        if (bodyDesc.shapes.Size() == 1 && bodyDesc.shapes[0].offset == glm::vec3(0.0f)) {
            shape = CreateShapeFromDesc(bodyDesc.shapes[0], runtime.colliders[0], ctx->assetManager);
        }
        else {
            JPH::StaticCompoundShapeSettings compound;
            bool bAnyNull = false;
            for (size_t i = 0; i < bodyDesc.shapes.Size(); ++i) {
                const Component::PhysicsShapeDesc& shapeDesc = bodyDesc.shapes[i];
                JPH::ShapeRefC subShape = CreateShapeFromDesc(shapeDesc, runtime.colliders[i], ctx->assetManager);
                if (!subShape) {
                    bAnyNull = true;
                    break;
                }
                compound.AddShape(
                    JPH::Vec3(shapeDesc.offset.x, shapeDesc.offset.y, shapeDesc.offset.z),
                    JPH::Quat(shapeDesc.rotation.x, shapeDesc.rotation.y, shapeDesc.rotation.z, shapeDesc.rotation.w),
                    subShape
                );
            }
            if (!bAnyNull) {
                auto r = compound.Create();
                if (!r.HasError()) { shape = r.Get(); }
            }
        }

        if (!shape) {
            const auto* name = state->registry.try_get<Component::NameComponent>(entity);
            LOG_WARN(Engine, "Shape creation failed for '{}', skipping", name ? name->name.c_str() : "Unnamed");
            resolved.PushBack(entity);
            continue;
        }

        runtime.shapeRef = shape;
        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::PendingPhysicsShapeCreationTag>(entity);
    }
}

void PhysicsBodyCreationResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;

    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PhysicsShapeRuntime, Component::PendingPhysicsBodyCreationTag>(
        entt::exclude<Component::PendingPhysicsShapeCreationTag>);

    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    const size_t budget = std::min(viewCount, Engine::MAX_ASSET_RESOLVES_PER_TICK);
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), budget);

    JPH::BodyInterface& bodyInterface = ctx->physicsWorld->Jolt().GetBodyInterface();

    for (const auto& [entity, bodyDesc, runtime] : view.each()) {
        if (resolved.Size() >= budget) { break; }
        if (!runtime.shapeRef) {
            resolved.PushBack(entity);
            continue;
        }

        auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
        if (!transform) {
            LOG_WARN(Engine, "PhysicsBodyDesc on entity without TransformComponent, skipping");
            resolved.PushBack(entity);
            continue;
        }

        // Drop any stale body so a re-tagged desc rebuilds cleanly.
        state->registry.remove<Component::PhysicsBodyComponent>(entity);
        state->registry.remove<Component::DynamicPhysicsBodyComponent>(entity);

        // Jolt is world-space. Place the body at the entity's world pose (equals local for an unparented entity).
        const Transform world = Component::ComputeWorldTransform(state->registry, entity);
        JPH::Vec3 pos(world.translation.x, world.translation.y, world.translation.z);
        JPH::Quat rot(world.rotation.x, world.rotation.y, world.rotation.z, world.rotation.w);
        JPH::BodyID bodyId = CreateBodyFromShape(bodyInterface, bodyDesc, runtime.shapeRef, pos, rot, Component::BodyUserDataFor(entity), bodyDesc.layerOverride);
        if (!bodyId.IsInvalid()) {
            if (bodyDesc.restitution > 0.0f) {
                bodyInterface.SetRestitution(bodyId, bodyDesc.restitution);
            }
            state->registry.emplace<Component::PhysicsBodyComponent>(entity, ToHandle(bodyId));
            if (bodyDesc.motionType == Component::PhysicsMotionType::Dynamic) {
                state->registry.emplace_or_replace<Component::DynamicPhysicsBodyComponent>(entity, world.translation, world.rotation, world.translation, world.rotation);
            }
        }
        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::PendingPhysicsBodyCreationTag>(entity);
    }
}
} // Engine
