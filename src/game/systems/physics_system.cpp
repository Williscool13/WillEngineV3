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
            bodyInterface.SetPositionAndRotation(
                physicsBody.bodyID,
                JPH::RVec3(transform.translation.x, transform.translation.y, transform.translation.z),
                JPH::Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w),
                JPH::EActivation::Activate
            );
        }
        state->registry.clear<Component::TeleportPhysicsTransformTag>();

        // Kinematic
        auto kinematicView = state->registry.view<Component::PhysicsBodyComponent, Component::TransformComponent, Component::DirtyKinematicPhysicsTransformTag>();
        for (const auto& [entity, physicsBody, transform] : kinematicView.each()) {
            bodyInterface.MoveKinematic(
                physicsBody.bodyID,
                JPH::RVec3(transform.translation.x, transform.translation.y, transform.translation.z),
                JPH::Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w),
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

        auto saveView = state->registry.view<Component::DynamicPhysicsBodyComponent, Component::TransformComponent>();
        for (auto [entity, dynamic, transform] : saveView.each()) {
            dynamic.previousPosition = transform.translation;
            dynamic.previousRotation = transform.rotation;
        }

        physics->Step(Physics::PHYSICS_TIMESTEP);

        auto dynamicView = state->registry.view<Component::DynamicPhysicsBodyComponent, Component::PhysicsBodyComponent, Component::TransformComponent>();
        for (auto [entity, dynamic, physicsBody, transform] : dynamicView.each()) {
            JPH::RVec3 pos = bodyInterface.GetPosition(physicsBody.bodyID);
            JPH::Quat rot = bodyInterface.GetRotation(physicsBody.bodyID);

            transform.translation = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
            transform.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
            state->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);

            /*glm::vec3 newPos(pos.GetX(), pos.GetY(), pos.GetZ());
            glm::quat newRot(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());

            constexpr float POS_EPSILON = 0.0001f;
            constexpr float ROT_EPSILON = 0.9999f;

            float dx = transform.translation.x - newPos.x;
            float dy = transform.translation.y - newPos.y;
            float dz = transform.translation.z - newPos.z;
            float distSq = dx*dx + dy*dy + dz*dz;
            bool posChanged = distSq > POS_EPSILON * POS_EPSILON;
            bool rotChanged = glm::abs(glm::dot(transform.rotation, newRot)) < ROT_EPSILON;

            if (posChanged || rotChanged) {
                transform.translation = newPos;
                transform.rotation = newRot;
                state->registry.emplace_or_replace<Component::DirtyRenderTransformTag>(entity);
            }*/
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
        if (state->registry.all_of<Component::DynamicPhysicsBodyComponent>(entity)) {
            state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(entity);
        }
        else {
            state->registry.emplace_or_replace<Component::DirtyKinematicPhysicsTransformTag>(entity);
        }
    }
}

void UpdatePhysicsEditor(Engine::EngineContext* ctx, Engine::EngineState* state)
{
    ZoneScoped;
    auto& bodyInterface = ctx->physicsSystem->GetBodyInterface();
    auto view = state->registry.view<Component::PhysicsBodyComponent, Component::TransformComponent, Component::DirtyTransformTag>();
    for (const auto& [entity, physicsBody, transform] : view.each()) {
        bodyInterface.SetPositionAndRotation(
            physicsBody.bodyID,
            JPH::RVec3(transform.translation.x, transform.translation.y, transform.translation.z),
            JPH::Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w),
            JPH::EActivation::DontActivate);
    }
}

void DebugRenderPhysics(Engine::EngineContext* ctx, Engine::EngineState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
#ifdef WDEBUG
    using PhysicsDebugMode = Engine::PhysicsDebugMode;
    if (state->editor.physicsDebugMode == PhysicsDebugMode::Off) { return; }

    if (state->bIsPlaying) {
        auto& filter = ctx->physicsSystem->GetDebugDrawFilter();
        filter.Clear();

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
    }
    else {
        constexpr glm::vec4 kDebugColor{0.2f, 0.8f, 1.0f, 1.0f};
        constexpr glm::vec4 kSensorColor{1.0f, 0.85f, 0.0f, 1.0f};
        auto& vf = frameBuffer->mainViewFamily;

        auto drawEntity = [&](const Component::PhysicsBodyDesc& bodyDesc, const Component::TransformComponent& transform) {
            const glm::vec4 color = bodyDesc.bIsSensor ? kSensorColor : kDebugColor;
            const glm::mat4 entityMat = glm::translate(glm::mat4(1.0f), transform.translation) * glm::mat4_cast(transform.rotation);
            for (const auto& shape : bodyDesc.shapes) {
                const glm::vec3 shapeCenter = glm::vec3(entityMat * glm::vec4(shape.offset, 1.0f));
                switch (shape.type) {
                    case Component::PhysicsShapeType::Box:
                        DEBUG_ADD_BOX(vf.debugBoxes, {shapeCenter, shape.box.halfExtents, transform.rotation * shape.rotation, color});
                        break;
                    case Component::PhysicsShapeType::Sphere:
                        DEBUG_ADD_SPHERE(vf.debugSpheres, {shapeCenter, shape.sphere.radius, color});
                        break;
                    case Component::PhysicsShapeType::Capsule:
                    {
                        const glm::vec3 top = shapeCenter + glm::vec3(0.0f, shape.capsule.halfHeight, 0.0f);
                        const glm::vec3 bot = shapeCenter - glm::vec3(0.0f, shape.capsule.halfHeight, 0.0f);
                        DEBUG_ADD_SPHERE(vf.debugSpheres, {top, shape.capsule.radius, color});
                        DEBUG_ADD_SPHERE(vf.debugSpheres, {bot, shape.capsule.radius, color});
                        DEBUG_ADD_LINE(vf.debugLines, {top + glm::vec3( shape.capsule.radius, 0, 0), bot + glm::vec3( shape.capsule.radius, 0, 0), color});
                        DEBUG_ADD_LINE(vf.debugLines, {top + glm::vec3(-shape.capsule.radius, 0, 0), bot + glm::vec3(-shape.capsule.radius, 0, 0), color});
                        DEBUG_ADD_LINE(vf.debugLines, {top + glm::vec3(0, 0, shape.capsule.radius), bot + glm::vec3(0, 0, shape.capsule.radius), color});
                        DEBUG_ADD_LINE(vf.debugLines, {top + glm::vec3(0, 0, -shape.capsule.radius), bot + glm::vec3(0, 0, -shape.capsule.radius), color});
                        break;
                    }
                    case Component::PhysicsShapeType::ConvexHull:
                    case Component::PhysicsShapeType::TriangleMesh:
                    {
                        const Engine::StaticModel* model = ctx->assetManager->GetModel(shape.meshSourceHandle);
                        if (model && model->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded && model->physicsCache && !model->physicsCache->indices.IsEmpty()) {
                            const auto& pos = model->physicsCache->positions;
                            const auto& idx = model->physicsCache->indices;
                            for (size_t i = 0; i + 2 < idx.Size(); i += 3) {
                                const glm::vec3 a = glm::vec3(entityMat * glm::vec4(pos[idx[i + 0]] * shape.bakedScale + shape.offset, 1.0f));
                                const glm::vec3 b = glm::vec3(entityMat * glm::vec4(pos[idx[i + 1]] * shape.bakedScale + shape.offset, 1.0f));
                                const glm::vec3 c = glm::vec3(entityMat * glm::vec4(pos[idx[i + 2]] * shape.bakedScale + shape.offset, 1.0f));
                                DEBUG_ADD_LINE(vf.debugLines, {a, b, color});
                                DEBUG_ADD_LINE(vf.debugLines, {b, c, color});
                                DEBUG_ADD_LINE(vf.debugLines, {c, a, color});
                            }
                        }
                        else {
                            DEBUG_ADD_SPHERE(vf.debugSpheres, {shapeCenter, 0.25f, color});
                        }
                        break;
                    }
                }
            }
        };

        if (state->editor.physicsDebugMode == PhysicsDebugMode::On) {
            for (const auto& [entity, bodyDesc, transform] : state->registry.view<Component::PhysicsBodyDesc, Component::TransformComponent>().each()) {
                drawEntity(bodyDesc, transform);
            }
        }
        else if (state->editor.physicsDebugMode == PhysicsDebugMode::SensorOnly) {
            for (const auto& [entity, bodyDesc, transform] : state->registry.view<Component::PhysicsBodyDesc, Component::TransformComponent>().each()) {
                if (bodyDesc.bIsSensor) { drawEntity(bodyDesc, transform); }
            }
        }
        else if (state->editor.physicsDebugMode == PhysicsDebugMode::Selected) {
            for (entt::entity entity : state->editor.selectedEntities) {
                const auto* bodyDesc = state->registry.try_get<Component::PhysicsBodyDesc>(entity);
                const auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
                if (bodyDesc && transform) { drawEntity(*bodyDesc, *transform); }
            }
        }
        else {
            // SensorAndTag
            for (const auto& [entity, bodyDesc, transform] : state->registry.view<Component::PhysicsBodyDesc, Component::TransformComponent>().each()) {
                if (bodyDesc.bIsSensor || state->registry.all_of<Component::DrawPhysicsDebugTag>(entity)) {
                    drawEntity(bodyDesc, transform);
                }
            }
        }
    }
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

JPH::ShapeRefC CreateShapeFromDesc(const Component::PhysicsShapeDesc& desc, Engine::AssetManager* assetManager)
{
    // todo hash dedupe
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
        case Component::PhysicsShapeType::ConvexHull:
        {
            if (!assetManager) { return nullptr; }
            auto* model = assetManager->GetModel(desc.meshSourceHandle);
            if (!model || !model->physicsCache) { return nullptr; }
            JPH::Array<JPH::Vec3> pts;
            pts.reserve(model->physicsCache->positions.Size());
            for (const auto& p : model->physicsCache->positions) {
                const glm::vec3 sp = p * desc.bakedScale;
                pts.push_back({sp.x, sp.y, sp.z});
            }
            JPH::ConvexHullShapeSettings s(pts);
            auto result = s.Create();
            if (result.HasError()) {
                LOG_WARN(Game, "ConvexHull shape creation failed: {}", result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }
        case Component::PhysicsShapeType::TriangleMesh:
        {
            if (!assetManager) { return nullptr; }
            auto* model = assetManager->GetModel(desc.meshSourceHandle);
            if (!model || !model->physicsCache) { return nullptr; }
            const auto& pos = model->physicsCache->positions;
            const auto& idx = model->physicsCache->indices;
            JPH::TriangleList tris;
            tris.reserve(idx.Size() / 3);
            const glm::vec3 sc = desc.bakedScale;
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
                LOG_WARN(Game, "TriangleMesh shape creation failed: {}", result.GetError().c_str());
                return nullptr;
            }
            return result.Get();
        }
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
            if (shapeDesc.type != Component::PhysicsShapeType::ConvexHull && shapeDesc.type != Component::PhysicsShapeType::TriangleMesh) { continue; }
            if (shapeDesc.meshSourceHandle.IsValid()) { continue; }

            if (shapeDesc.meshSourceModelId.IsValid()) {
                if (ctx->assetManager->IsModelFrozen(shapeDesc.meshSourceModelId)) { allArmed = false; break; }
                shapeDesc.meshSourceHandle = ctx->assetManager->LoadModel(shapeDesc.meshSourceModelId);
            }
            else if (!std::holds_alternative<std::monostate>(shapeDesc.proceduralParams)) {
                shapeDesc.meshSourceHandle = ctx->assetManager->LoadProceduralModel(shapeDesc.proceduralParams);
            }
            else if (!shapeDesc.splineParams.spline.points.IsEmpty()) {
                shapeDesc.meshSourceHandle = ctx->assetManager->LoadSplineModel(shapeDesc.splineParams);
            }
            else if (shapeDesc.text3DSource.IsValid()) {
                const Component::Text3DShapeSource& t = shapeDesc.text3DSource;
                if (ctx->assetManager->IsFontFrozen(t.fontId)) { allArmed = false; break; }
                shapeDesc.meshSourceHandle = ctx->assetManager->LoadText3DModel(t.fontId, t.text, t.depth, t.flatness, t.tracking, t.scale, t.bSmoothNormals);
            }
            if (!shapeDesc.meshSourceHandle.IsValid()) {
                LOG_WARN(Game, "Physics mesh source could not be loaded. Removing pending tag.");
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
            if (shapeDesc.type != Component::PhysicsShapeType::ConvexHull && shapeDesc.type != Component::PhysicsShapeType::TriangleMesh) { continue; }

            auto* model = ctx->assetManager->GetModel(shapeDesc.meshSourceHandle);
            if (!model) {
                LOG_WARN(Game, "Physics mesh source model not found. Removing loading tag.");
                shouldAbandon = true;
                break;
            }
            if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
                LOG_WARN(Game, "Physics mesh source model failed to load. Removing loading tag.");
                shouldAbandon = true;
                break;
            }
            if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
                allReady = false;
                break;
            }
            if (!model->physicsCache) {
                LOG_WARN(Game, "Physics mesh cache unavailable. Removing loading tag.");
                shouldAbandon = true;
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
            // Only these 2 need to verify source mesh
            if (shape.type != Component::PhysicsShapeType::ConvexHull && shape.type != Component::PhysicsShapeType::TriangleMesh) {
                continue;
            }

            if (!shape.meshSourceModelId.IsValid() && std::holds_alternative<std::monostate>(shape.proceduralParams) && shape.splineParams.spline.points.IsEmpty()) {
                LOG_WARN(Game, "PhysicsBodyDesc has mesh shape with no mesh source, skipping shape creation");
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

        JPH::Vec3 pos(transform->translation.x, transform->translation.y, transform->translation.z);
        JPH::Quat rot(transform->rotation.x, transform->rotation.y, transform->rotation.z, transform->rotation.w);
        JPH::BodyID bodyId = CreateBodyFromShape(bodyInterface, bodyDesc, pos, rot, bodyDesc.layerOverride);
        if (!bodyId.IsInvalid()) {
            if (bodyDesc.restitution > 0.0f) {
                bodyInterface.SetRestitution(bodyId, bodyDesc.restitution);
            }
            state->registry.emplace<Component::PhysicsBodyComponent>(entity, bodyId);
            if (bodyDesc.motionType == Component::PhysicsMotionType::Dynamic) {
                state->registry.emplace_or_replace<Component::DynamicPhysicsBodyComponent>(entity, transform->translation, transform->rotation);
            }
        }
        resolved.PushBack(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::PendingPhysicsBodyCreationTag>(entity);
    }
}
} // Game
