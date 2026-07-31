//
// Created by William on 2025-12-25.
//

#include "physics_system.h"
#include "physics/physics_system.h"

#include <tracy/Tracy.hpp>

#include "core/containers/arena_fixed_vector.h"
#include "engine/include/engine_context.h"
#include "core/time/time_frame.h"
#include "game/fwd_components.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/components/physics/physics_body_component.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
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

namespace Game
{
void ConnectPhysicsObservers(entt::registry& registry)
{
    registry.on_construct<Component::PhysicsBodyDesc>().connect<&Component::PhysicsBodyDesc::OnConstruct>();
    registry.on_update<Component::PhysicsBodyDesc>().connect<&Component::PhysicsBodyDesc::OnUpdate>();
    registry.on_destroy<Component::PhysicsBodyDesc>().connect<&Component::PhysicsBodyDesc::OnDestroy>();

    registry.on_construct<Component::PhysicsBodyComponent>().connect<&Component::PhysicsBodyComponent::OnConstruct>();
    registry.on_destroy<Component::PhysicsBodyComponent>().connect<&Component::PhysicsBodyComponent::OnDestroy>();
}

void DisconnectPhysicsObservers(entt::registry& registry)
{
    registry.on_construct<Component::PhysicsBodyDesc>().disconnect<&Component::PhysicsBodyDesc::OnConstruct>();
    registry.on_update<Component::PhysicsBodyDesc>().disconnect<&Component::PhysicsBodyDesc::OnUpdate>();
    registry.on_destroy<Component::PhysicsBodyDesc>().disconnect<&Component::PhysicsBodyDesc::OnDestroy>();

    registry.on_construct<Component::PhysicsBodyComponent>().disconnect<&Component::PhysicsBodyComponent::OnConstruct>();
    registry.on_destroy<Component::PhysicsBodyComponent>().disconnect<&Component::PhysicsBodyComponent::OnDestroy>();
}

void PhysicsUpdate(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto* physics = ctx->physicsSystem;
    state->physics.deltaTimeAccumulator += state->timeFrame->deltaTime;

    // todo: Truly teleport mechanics are a little more user-defined. Need a unified "mark movement as teleport" function that emplaces the teleport physics transform tag to allow kinematics to teleport.
    while (state->physics.deltaTimeAccumulator >= Physics::PHYSICS_TIMESTEP) {
        auto& bodyInterface = physics->GetBodyInterface();

        // Teleport
        auto teleportView = state->registry.view<Component::PhysicsBodyComponent, Component::TransformComponent, Component::TeleportPhysicsTransformTag>();
        for (const auto& [entity, physicsBody, transform] : teleportView.each()) {
            const Transform world = Component::ComputeWorldTransform(state->registry, entity);
            bodyInterface.SetPositionAndRotation(
                physicsBody.bodyID,
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
        auto kinematicView = state->registry.view<Component::PhysicsBodyComponent, Component::TransformComponent, Component::DirtyKinematicPhysicsTransformTag>();
        for (const auto& [entity, physicsBody, transform] : kinematicView.each()) {
            const Transform world = Component::ComputeWorldTransform(state->registry, entity);
            bodyInterface.MoveKinematic(
                physicsBody.bodyID,
                JPH::RVec3(world.translation.x, world.translation.y, world.translation.z),
                JPH::Quat(world.rotation.x, world.rotation.y, world.rotation.z, world.rotation.w),
                Physics::PHYSICS_TIMESTEP
            );
        }
        state->registry.clear<Component::DirtyKinematicPhysicsTransformTag>();

        // Set velocity
        auto velocityView = state->registry.view<Component::PhysicsBodyComponent, Component::SetVelocityTag>();
        for (const auto& [entity, physicsBody, tag] : velocityView.each()) {
            bodyInterface.SetLinearAndAngularVelocity(
                physicsBody.bodyID,
                JPH::Vec3(tag.linearVelocity.x, tag.linearVelocity.y, tag.linearVelocity.z),
                JPH::Vec3(tag.angularVelocity.x, tag.angularVelocity.y, tag.angularVelocity.z)
            );
        }
        state->registry.clear<Component::SetVelocityTag>();

        auto saveView = state->registry.view<Component::DynamicPhysicsBodyComponent>();
        for (auto [entity, dynamic] : saveView.each()) {
            dynamic.previousPosition = dynamic.currentPosition;
            dynamic.previousRotation = dynamic.currentRotation;
        }

        physics->Step(Physics::PHYSICS_TIMESTEP);

        auto dynamicView = state->registry.view<Component::DynamicPhysicsBodyComponent, Component::PhysicsBodyComponent, Component::TransformComponent>();
        for (auto [entity, dynamic, physicsBody, transform] : dynamicView.each()) {
            JPH::RVec3 pos = bodyInterface.GetPosition(physicsBody.bodyID);
            JPH::Quat rot = bodyInterface.GetRotation(physicsBody.bodyID);

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
    }

    state->physics.interpolationAlpha = state->physics.deltaTimeAccumulator / Physics::PHYSICS_TIMESTEP;
}

void ResolveCollisionEvents(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    state->physics.resolvedAddedEvents.Clear();
    for (const auto& event : ctx->physicsSystem->GetAddedEvents()) {
        auto it1 = state->physics.bodyToEntity.Find(event.body1);
        auto it2 = state->physics.bodyToEntity.Find(event.body2);
        state->physics.resolvedAddedEvents.PushBack({
            it1 != nullptr ? *it1 : entt::null,
            it2 != nullptr ? *it2 : entt::null,
            {event.worldNormal.GetX(), event.worldNormal.GetY(), event.worldNormal.GetZ()},
            {event.contactPoint.GetX(), event.contactPoint.GetY(), event.contactPoint.GetZ()},
            event.penetrationDepth
        });
    }

    state->physics.resolvedPersistedEvents.Clear();
    for (const auto& event : ctx->physicsSystem->GetPersistedEvents()) {
        auto it1 = state->physics.bodyToEntity.Find(event.body1);
        auto it2 = state->physics.bodyToEntity.Find(event.body2);
        state->physics.resolvedPersistedEvents.PushBack({
            it1 != nullptr ? *it1 : entt::null,
            it2 != nullptr ? *it2 : entt::null,
            {event.worldNormal.GetX(), event.worldNormal.GetY(), event.worldNormal.GetZ()},
            {event.contactPoint.GetX(), event.contactPoint.GetY(), event.contactPoint.GetZ()},
            event.penetrationDepth
        });
    }

    state->physics.resolvedRemovedEvents.Clear();
    for (const auto& event : ctx->physicsSystem->GetRemovedEvents()) {
        auto it1 = state->physics.bodyToEntity.Find(event.body1);
        auto it2 = state->physics.bodyToEntity.Find(event.body2);
        state->physics.resolvedRemovedEvents.PushBack({
            it1 != nullptr ? *it1 : entt::null,
            it2 != nullptr ? *it2 : entt::null,
        });
    }
}

void MarkPhysicsTransformsDirty(Engine::EngineState* state)
{
    auto view = state->registry.view<Component::PhysicsBodyComponent, Component::DirtyTransformTag>();
    for (auto entity : view) {
        const auto* desc = state->registry.try_get<Component::PhysicsBodyDesc>(entity);
        if (desc && desc->motionType == Component::PhysicsMotionType::Kinematic) {
            state->registry.emplace_or_replace<Component::DirtyKinematicPhysicsTransformTag>(entity);
        }
        else {
            state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(entity);
        }
    }
}

void UpdatePhysicsEditor(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();
    auto view = state->registry.view<Component::PhysicsBodyComponent, Component::TransformComponent, Component::DirtyTransformTag>();
    for (const auto& [entity, physicsBody, transform] : view.each()) {
        const Transform world = Component::ComputeWorldTransform(state->registry, entity);
        bodyInterface.SetPositionAndRotation(
            physicsBody.bodyID,
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
    using PhysicsDebugMode = Engine::PhysicsDebugMode;
    if (state->editor.physicsDebugMode == PhysicsDebugMode::Off) { return; }

    auto& filter = ctx->physicsSystem->GetDebugDrawFilter();
    filter.Clear();

    ctx->physicsSystem->SetDebugDrawForceLowestLOD(state->editor.physicsDebugMode == PhysicsDebugMode::On);

    if (state->editor.physicsDebugMode == PhysicsDebugMode::On) {
        auto view = state->registry.view<Component::PhysicsBodyComponent>();
        for (const auto& [entity, physicsBody] : view.each()) {
            filter.AddBody(physicsBody.bodyID);
        }
    }
    else if (state->editor.physicsDebugMode == PhysicsDebugMode::SensorOnly) {
        auto view = state->registry.view<Component::PhysicsBodyComponent, Component::PhysicsBodyDesc>();
        for (const auto& [entity, physicsBody, bodyDesc] : view.each()) {
            if (bodyDesc.bIsSensor) { filter.AddBody(physicsBody.bodyID); }
        }
    }
    else if (state->editor.physicsDebugMode == PhysicsDebugMode::Selected) {
        for (entt::entity entity : state->editor.selectedEntities) {
            if (auto* physicsBody = state->registry.try_get<Component::PhysicsBodyComponent>(entity)) {
                filter.AddBody(physicsBody->bodyID);
            }
        }
    }
    else {
        // SensorAndTag
        auto sensorView = state->registry.view<Component::PhysicsBodyComponent, Component::PhysicsBodyDesc>();
        for (const auto& [entity, physicsBody, bodyDesc] : sensorView.each()) {
            if (bodyDesc.bIsSensor) { filter.AddBody(physicsBody.bodyID); }
        }
        auto tagView = state->registry.view<Component::DrawPhysicsDebugTag, Component::PhysicsBodyComponent>();
        for (const auto& [entity, physicsBody] : tagView.each()) {
            filter.AddBody(physicsBody.bodyID);
        }
    }

    ctx->physicsSystem->DrawDebug(&frameBuffer->mainViewFamily);
#endif
}


JPH::BodyID CreateBodyFromShape(JPH::BodyInterface& bodyInterface, const Component::PhysicsBodyDesc& desc, JPH::RVec3 position, JPH::Quat rotation, JPH::ObjectLayer layerOverride)
{
    JPH::EMotionType motionType = desc.motionType == Component::PhysicsMotionType::Static
                                      ? JPH::EMotionType::Static
                                      : desc.motionType == Component::PhysicsMotionType::Dynamic
                                            ? JPH::EMotionType::Dynamic
                                            : JPH::EMotionType::Kinematic;

    JPH::ObjectLayer layer;
    if (layerOverride != static_cast<JPH::ObjectLayer>(0xFFFF)) {
        layer = layerOverride;
    }
    else if (desc.bIsSensor) {
        layer = Physics::Layers::SENSOR;
    }
    else {
        layer = desc.motionType == Component::PhysicsMotionType::Static ? Physics::Layers::NON_MOVING : Physics::Layers::MOVING;
    }

    JPH::BodyCreationSettings settings(desc.shapeRef, position, rotation, motionType, layer);
    if (desc.motionType != Component::PhysicsMotionType::Static) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    settings.mFriction = desc.friction;
    settings.mMotionQuality = desc.motionQuality;
    settings.mEnhancedInternalEdgeRemoval = desc.bEnhancedInternalEdgeRemoval;
    settings.mIsSensor = desc.bIsSensor;

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
                        return r.HasError() ? JPH::ShapeRefC{} : r.Get();
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
                LOG_WARN(Game, "Compound shape creation failed: {}", result.GetError().c_str());
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
                LOG_WARN(Game, "ConvexHull collider creation failed: {}", result.GetError().c_str());
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
                LOG_WARN(Game, "TriangleMesh collider creation failed: {}", result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }
    }
    return nullptr;
}

JPH::ShapeRefC CreateShapeFromDesc(const Component::PhysicsShapeDesc& desc, Engine::AssetManager* assetManager)
{
    if (desc.colliderHandle.IsValid() && assetManager) {
        auto* collider = assetManager->GetCollider(desc.colliderHandle);
        if (!collider || collider->loadState != Engine::PhysicsColliderAsset::LoadState::Loaded) { return nullptr; }
        return CreateShapeFromCollider(*collider, desc.bakedScale);
    }

    switch (desc.type) {
        case Component::PhysicsShapeType::Box:
        {
            JPH::BoxShapeSettings s(JPH::Vec3(desc.box.halfExtents.x, desc.box.halfExtents.y, desc.box.halfExtents.z));
            return s.Create().Get();
        }
        case Component::PhysicsShapeType::Sphere:
        {
            JPH::SphereShapeSettings s(desc.sphere.radius);
            return s.Create().Get();
        }
        case Component::PhysicsShapeType::Capsule:
        {
            JPH::CapsuleShapeSettings s(desc.capsule.halfHeight, desc.capsule.radius);
            return s.Create().Get();
        }
        case Component::PhysicsShapeType::Collider:
            // Resolved above via colliderHandle; a Collider with no valid handle has nothing to build.
            return nullptr;
    }
    return nullptr;
}

void PhysicsMeshPendingKickoff(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PendingPhysicsMeshTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    auto started = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    auto abandoned = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (auto [entity, bodyDesc] : view.each()) {
        bool allArmed = true;
        bool shouldAbandon = false;

        for (auto& shapeDesc : bodyDesc.shapes) {
            if (shapeDesc.type != Component::PhysicsShapeType::Collider) { continue; }
            if (shapeDesc.colliderHandle.IsValid()) { continue; }

            if (shapeDesc.meshSourceModelId.IsValid()) {
                if (ctx->assetManager->IsModelFrozen(shapeDesc.meshSourceModelId)) { allArmed = false; break; }

                const bool bWantsMesh = shapeDesc.bMeshPrecise && bodyDesc.motionType != Component::PhysicsMotionType::Dynamic;
                const Engine::PhysicsColliderKind kind = bWantsMesh ? Engine::PhysicsColliderKind::TriangleMesh : Engine::PhysicsColliderKind::ConvexHull;
                shapeDesc.colliderHandle = ctx->assetManager->LoadModelCollider(shapeDesc.meshSourceModelId, kind);
            }
            else if (!std::holds_alternative<std::monostate>(shapeDesc.proceduralParams)) {
                shapeDesc.colliderHandle = ctx->assetManager->LoadProceduralCollider(shapeDesc.proceduralParams);
            }
            else if (!shapeDesc.splineParams.spline.points.IsEmpty()) {
                shapeDesc.colliderHandle = ctx->assetManager->LoadSplineCollider(shapeDesc.splineParams);
            }
            else if (shapeDesc.text3DSource.IsValid()) {
                const Component::Text3DShapeSource& t = shapeDesc.text3DSource;
                if (ctx->assetManager->IsFontFrozen(t.fontId)) { allArmed = false; break; }
                shapeDesc.colliderHandle = ctx->assetManager->LoadText3DCollider(t.fontId, t.text, t.depth, t.flatness, t.tracking, t.scale, t.bSmoothNormals, t.align, t.anchor, t.bPrecise);
            }

            if (!shapeDesc.colliderHandle.IsValid()) {
                LOG_WARN(Game, "Physics collider source could not be loaded. Removing pending tag.");
                shouldAbandon = true;
                break;
            }
        }

        if (shouldAbandon) {
            abandoned.PushBack(entity);
            continue;
        }
        if (!allArmed) {
            state->bPendingModelResolve = true; // a source is frozen; stay pending until it drains
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
        state->bPendingModelResolve = true;
    }
}

void PhysicsMeshLoadResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PhysicsMeshLoadingTag>();
    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);
    for (auto [entity, bodyDesc] : view.each()) {
        bool allReady = true;
        bool shouldAbandon = false;

        for (const auto& shapeDesc : bodyDesc.shapes) {
            if (!shapeDesc.colliderHandle.IsValid()) { continue; }

            auto* collider = ctx->assetManager->GetCollider(shapeDesc.colliderHandle);
            if (!collider) {
                LOG_WARN(Game, "Physics collider not found. Removing loading tag.");
                shouldAbandon = true;
                break;
            }
            if (collider->loadState == Engine::PhysicsColliderAsset::LoadState::FailedToLoad) {
                LOG_WARN(Game, "Physics collider failed to load. Removing loading tag.");
                shouldAbandon = true;
                break;
            }
            if (collider->loadState != Engine::PhysicsColliderAsset::LoadState::Loaded) {
                allReady = false;
                break;
            }
        }

        if (!allReady && !shouldAbandon) {
            state->bPendingModelResolve = true;
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
    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PendingPhysicsShapeCreationTag>(
        entt::exclude<Component::PendingPhysicsMeshTag, Component::PhysicsMeshLoadingTag>);

    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);

    for (const auto& [entity, bodyDesc] : view.each()) {
        bool bDegenerate = false;
        for (const auto& shape : bodyDesc.shapes) {
            if (shape.type != Component::PhysicsShapeType::Collider) { continue; }

            const bool bHasSource = shape.meshSourceModelId.IsValid()
                                    || !std::holds_alternative<std::monostate>(shape.proceduralParams)
                                    || !shape.splineParams.spline.points.IsEmpty()
                                    || shape.text3DSource.IsValid();
            if (!bHasSource) {
                LOG_WARN(Game, "PhysicsBodyDesc has a Collider shape with no source, skipping shape creation");
                bDegenerate = true;
                break;
            }
        }
        if (bDegenerate) {
            resolved.PushBack(entity);
            continue;
        }

        if (bodyDesc.shapes.IsEmpty()) {
            LOG_WARN(Game, "PhysicsBodyDesc has no shapes, skipping shape creation");
            resolved.PushBack(entity);
            continue;
        }

        JPH::ShapeRefC shape;
        if (bodyDesc.shapes.Size() == 1 && bodyDesc.shapes[0].offset == glm::vec3(0.0f)) {
            shape = CreateShapeFromDesc(bodyDesc.shapes[0], ctx->assetManager);
        }
        else {
            JPH::StaticCompoundShapeSettings compound;
            bool bAnyNull = false;
            for (const auto& shapeDesc : bodyDesc.shapes) {
                JPH::ShapeRefC subShape = CreateShapeFromDesc(shapeDesc, ctx->assetManager);
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
            if (!bAnyNull) { shape = compound.Create().Get(); }
        }

        if (!shape) {
            LOG_WARN(Game, "Shape creation failed, skipping");
            resolved.PushBack(entity);
            continue;
        }

        bodyDesc.shapeRef = shape;
        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::PendingPhysicsShapeCreationTag>(entity);
    }
}

void PhysicsBodyCreationResolve(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;

    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PendingPhysicsBodyCreationTag>(
        entt::exclude<Component::PendingPhysicsShapeCreationTag>);

    size_t viewCount = view.size_hint();
    if (viewCount == 0) {
        return;
    }
    auto resolved = Core::ArenaFixedVector<entt::entity>(&ctx->gameplayArena.Get(), viewCount);

    JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    for (const auto& [entity, bodyDesc] : view.each()) {
        if (!bodyDesc.shapeRef) {
            resolved.PushBack(entity);
            continue;
        }

        auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
        if (!transform) {
            LOG_WARN(Game, "PhysicsBodyDesc on entity without TransformComponent, skipping");
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
        JPH::BodyID bodyId = CreateBodyFromShape(bodyInterface, bodyDesc, pos, rot, bodyDesc.layerOverride);
        if (!bodyId.IsInvalid()) {
            if (bodyDesc.restitution > 0.0f) {
                bodyInterface.SetRestitution(bodyId, bodyDesc.restitution);
            }
            state->registry.emplace<Component::PhysicsBodyComponent>(entity, bodyId);
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
} // Game
