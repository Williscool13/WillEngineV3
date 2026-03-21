//
// Created by William on 2025-12-25.
//

#include "physics_system.h"
#include "physics/physics_system.h"

#include <tracy/Tracy.hpp>

#include "core/include/engine_context.h"
#include "core/time/time_frame.h"
#include "game/fwd_components.h"
#include "engine/asset_manager.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "game/components/physics/physics_body_component.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"

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

void UpdatePhysics(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    auto* physics = ctx->physicsSystem;
    state->physicsDeltaTimeAccumulator += state->timeFrame->deltaTime;

    auto transformDirtyView = state->registry.view<Component::PhysicsBodyComponent, Component::DirtyTransformTag>();
    for (auto entity : transformDirtyView) {
        state->registry.emplace_or_replace<Component::TeleportPhysicsTransformTag>(entity);
    }

    while (state->physicsDeltaTimeAccumulator >= Physics::PHYSICS_TIMESTEP) {
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

        state->physicsDeltaTimeAccumulator -= Physics::PHYSICS_TIMESTEP;
    }

    state->physicsInterpolationAlpha = state->physicsDeltaTimeAccumulator / Physics::PHYSICS_TIMESTEP;
}

void DebugRenderPhysics(Core::EngineContext* ctx, Engine::GameState* state, Core::FrameBuffer* frameBuffer)
{
    ZoneScoped;
#ifndef PACKAGED_BUILD
    using PhysicsDebugMode = Engine::GameState::PhysicsDebugMode;
    if (state->physicsDebugMode == PhysicsDebugMode::Off) { return; }

    if (state->bIsPlaying) {
        auto& filter = ctx->physicsSystem->GetDebugDrawFilter();
        filter.Clear();

        if (state->physicsDebugMode == PhysicsDebugMode::On) {
            auto view = state->registry.view<Component::PhysicsBodyComponent>();
            for (const auto& [entity, physicsBody] : view.each()) {
                filter.AddBody(physicsBody.bodyID);
            }
        }
        else {
            auto view = state->registry.view<Component::DrawPhysicsDebugTag, Component::PhysicsBodyComponent>();
            for (const auto& [entity, physicsBody] : view.each()) {
                filter.AddBody(physicsBody.bodyID);
            }
        }

        ctx->physicsSystem->DrawDebug(&frameBuffer->mainViewFamily);
    }
    else {
        constexpr glm::vec4 kDebugColor{0.2f, 0.8f, 1.0f, 1.0f};
        auto& vf = frameBuffer->mainViewFamily;

        auto drawEntity = [&](const Component::PhysicsBodyDesc& bodyDesc, const Component::TransformComponent& transform) {
            const glm::mat4 entityMat = glm::translate(glm::mat4(1.0f), transform.translation) * glm::mat4_cast(transform.rotation);
            for (const auto& shape : bodyDesc.shapes) {
                const glm::vec3 shapeCenter = glm::vec3(entityMat * glm::vec4(shape.offset, 1.0f));
                switch (shape.type) {
                    case Component::PhysicsShapeType::Box:
                        DEBUG_ADD_BOX(vf.debugBoxes, {shapeCenter, shape.box.halfExtents, transform.rotation * shape.rotation, kDebugColor});
                        break;
                    case Component::PhysicsShapeType::Sphere:
                        DEBUG_ADD_SPHERE(vf.debugSpheres, {shapeCenter, shape.sphere.radius, kDebugColor});
                        break;
                    case Component::PhysicsShapeType::Capsule:
                    {
                        const glm::vec3 top = shapeCenter + glm::vec3(0.0f, shape.capsule.halfHeight, 0.0f);
                        const glm::vec3 bot = shapeCenter - glm::vec3(0.0f, shape.capsule.halfHeight, 0.0f);
                        DEBUG_ADD_SPHERE(vf.debugSpheres, {top, shape.capsule.radius, kDebugColor});
                        DEBUG_ADD_SPHERE(vf.debugSpheres, {bot, shape.capsule.radius, kDebugColor});
                        DEBUG_ADD_LINE(vf.debugLines, {top + glm::vec3( shape.capsule.radius, 0, 0), bot + glm::vec3( shape.capsule.radius, 0, 0), kDebugColor});
                        DEBUG_ADD_LINE(vf.debugLines, {top + glm::vec3(-shape.capsule.radius, 0, 0), bot + glm::vec3(-shape.capsule.radius, 0, 0), kDebugColor});
                        DEBUG_ADD_LINE(vf.debugLines, {top + glm::vec3(0, 0, shape.capsule.radius), bot + glm::vec3(0, 0, shape.capsule.radius), kDebugColor});
                        DEBUG_ADD_LINE(vf.debugLines, {top + glm::vec3(0, 0, -shape.capsule.radius), bot + glm::vec3(0, 0, -shape.capsule.radius), kDebugColor});
                        break;
                    }
                    case Component::PhysicsShapeType::ConvexHull:
                    case Component::PhysicsShapeType::TriangleMesh:
                    {
                        const Engine::StaticModel* model = ctx->assetManager->GetModel(shape.meshSourceHandle);
                        if (model && model->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded && model->physicsCache && !model->physicsCache->indices.empty()) {
                            const auto& pos = model->physicsCache->positions;
                            const auto& idx = model->physicsCache->indices;
                            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                                const glm::vec3 a = glm::vec3(entityMat * glm::vec4(pos[idx[i + 0]] + shape.offset, 1.0f));
                                const glm::vec3 b = glm::vec3(entityMat * glm::vec4(pos[idx[i + 1]] + shape.offset, 1.0f));
                                const glm::vec3 c = glm::vec3(entityMat * glm::vec4(pos[idx[i + 2]] + shape.offset, 1.0f));
                                DEBUG_ADD_LINE(vf.debugLines, {a, b, kDebugColor});
                                DEBUG_ADD_LINE(vf.debugLines, {b, c, kDebugColor});
                                DEBUG_ADD_LINE(vf.debugLines, {c, a, kDebugColor});
                            }
                        }
                        else {
                            DEBUG_ADD_SPHERE(vf.debugSpheres, {shapeCenter, 0.25f, kDebugColor});
                        }
                        break;
                    }
                }
            }
        };

        if (state->physicsDebugMode == PhysicsDebugMode::On) {
            for (const auto& [entity, bodyDesc, transform] : state->registry.view<Component::PhysicsBodyDesc, Component::TransformComponent>().each()) {
                drawEntity(bodyDesc, transform);
            }
        }
        else {
            // TagOnly
            for (const auto& [entity, bodyDesc, transform] : state->registry.view<Component::DrawPhysicsDebugTag, Component::PhysicsBodyDesc, Component::TransformComponent>().each()) {
                drawEntity(bodyDesc, transform);
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

    JPH::ObjectLayer layer = layerOverride != JPH::ObjectLayer(0xFFFF)
                                 ? layerOverride
                                 : desc.motionType == Component::PhysicsMotionType::Static ? Physics::Layers::NON_MOVING : Physics::Layers::MOVING;

    JPH::BodyCreationSettings settings(desc.shapeRef, position, rotation, motionType, layer);
    if (desc.motionType == Component::PhysicsMotionType::Dynamic) {
        settings.mMassPropertiesOverride.mMass = desc.mass;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    }
    settings.mFriction = desc.friction;
    settings.mMotionQuality = desc.motionQuality;

    return bodyInterface.CreateAndAddBody(settings, desc.motionType == Component::PhysicsMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
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
            pts.reserve(model->physicsCache->positions.size());
            for (const auto& p : model->physicsCache->positions) {
                pts.push_back({p.x, p.y, p.z});
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
            tris.reserve(idx.size() / 3);
            for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                tris.push_back(JPH::Triangle(
                    JPH::Float3(pos[idx[i]].x, pos[idx[i]].y, pos[idx[i]].z),
                    JPH::Float3(pos[idx[i + 1]].x, pos[idx[i + 1]].y, pos[idx[i + 1]].z),
                    JPH::Float3(pos[idx[i + 2]].x, pos[idx[i + 2]].y, pos[idx[i + 2]].z)));
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

void ResolvePhysicsMeshLoads(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    std::vector<entt::entity> resolved;

    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PendingPhysicsMeshTag>();
    for (const auto& [entity, bodyDesc] : view.each()) {
        bool allReady = true;
        bool shouldAbandon = false;

        for (const auto& shapeDesc : bodyDesc.shapes) {
            if (shapeDesc.type != Component::PhysicsShapeType::ConvexHull && shapeDesc.type != Component::PhysicsShapeType::TriangleMesh) continue;
            auto* model = ctx->assetManager->GetModel(shapeDesc.meshSourceHandle);
            if (!model) {
                LOG_WARN(Game, "Physics mesh source model not found. Removing pending tag.");
                shouldAbandon = true;
                break;
            }
            if (model->modelLoadState == Engine::StaticModel::ModelLoadState::FailedToLoad) {
                LOG_WARN(Game, "Physics mesh source model failed to load. Removing pending tag.");
                shouldAbandon = true;
                break;
            }
            if (model->modelLoadState != Engine::StaticModel::ModelLoadState::Loaded) {
                allReady = false;
                break;
            }
            if (!model->physicsCache) {
                LOG_WARN(Game, "Physics mesh cache unavailable. Removing pending tag.");
                shouldAbandon = true;
                break;
            }
        }

        if (!allReady && !shouldAbandon) {
            state->bPendingModelResolve = true;
            continue;
        }

        resolved.push_back(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::PendingPhysicsMeshTag>(entity);
    }
}

void ResolvePhysicsShapeCreation(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    std::vector<entt::entity> resolved;

    // Mesh based physics need to wait for its mesh to load
    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PendingPhysicsShapeCreationTag>(
        entt::exclude<Component::PendingPhysicsMeshTag>);

    for (const auto& [entity, bodyDesc] : view.each()) {
        bool bDegenerate = false;
        for (const auto& shape : bodyDesc.shapes) {
            // Only these 2 need to verify source mesh
            if (shape.type != Component::PhysicsShapeType::ConvexHull && shape.type != Component::PhysicsShapeType::TriangleMesh) {
                continue;
            }

            if (!shape.meshSourceModelId.IsValid() && std::holds_alternative<std::monostate>(shape.proceduralParams) && shape.splineParams.controlPoints.empty()) {
                LOG_WARN(Game, "PhysicsBodyDesc has mesh shape with no mesh source, skipping shape creation");
                bDegenerate = true;
                break;
            }
        }
        if (bDegenerate) {
            resolved.push_back(entity);
            continue;
        }

        if (bodyDesc.shapes.empty()) {
            LOG_WARN(Game, "PhysicsBodyDesc has no shapes, skipping shape creation");
            resolved.push_back(entity);
            continue;
        }

        JPH::ShapeRefC shape;
        if (bodyDesc.shapes.size() == 1 && bodyDesc.shapes[0].offset == glm::vec3(0.0f)) {
            shape = CreateShapeFromDesc(bodyDesc.shapes[0], ctx->assetManager);
        }
        else {
            JPH::StaticCompoundShapeSettings compound;
            bool bAnyNull = false;
            for (const auto& shapeDesc : bodyDesc.shapes) {
                JPH::ShapeRefC subShape = CreateShapeFromDesc(shapeDesc, ctx->assetManager);
                if (!subShape) { bAnyNull = true; break; }
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
            resolved.push_back(entity);
            continue;
        }

        bodyDesc.shapeRef = shape;
        resolved.push_back(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::PendingPhysicsShapeCreationTag>(entity);
    }
}

void ResolvePhysicsBodyCreation(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    if (!state->bIsPlaying) return;

    auto view = state->registry.view<Component::PhysicsBodyDesc>(
        entt::exclude<Component::PhysicsBodyComponent, Component::PendingPhysicsShapeCreationTag>);

    for (const auto& [entity, bodyDesc] : view.each()) {
        if (!bodyDesc.shapeRef) {
            continue;
        }

        auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
        if (!transform) {
            LOG_WARN(Game, "PhysicsBodyDesc on entity without TransformComponent, skipping");
            continue;
        }

        JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();

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
    }
}
void PhysicsOnPlayStop(Core::EngineContext* ctx, Engine::GameState* state)
{
    JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();

    auto view = state->registry.view<Component::PhysicsBodyComponent>();
    for (const auto& [entity, body] : view.each()) {
        if (!body.bodyID.IsInvalid()) {
            bodyInterface.RemoveBody(body.bodyID);
            bodyInterface.DestroyBody(body.bodyID);
        }
    }
    state->registry.clear<Component::PhysicsBodyComponent>();
    state->registry.clear<Component::DynamicPhysicsBodyComponent>();
}
} // Game
