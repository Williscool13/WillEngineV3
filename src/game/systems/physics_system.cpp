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
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/StaticCompoundShape.h"

namespace Game
{
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
                        DEBUG_ADD_SPHERE(vf.debugSpheres, {shapeCenter, 0.25f, kDebugColor});
                        break;
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

JPH::BodyID CreateBodyFromDesc(JPH::BodyInterface& bodyInterface, const Component::PhysicsBodyDesc& desc, JPH::RVec3 position, JPH::Quat rotation, Engine::AssetManager* assetManager)
{
    JPH::ShapeRefC shape;

    if (desc.shapes.empty()) {
        return JPH::BodyID(JPH::BodyID::cInvalidBodyID);
    }
    if (desc.shapes.size() == 1 && desc.shapes[0].offset == glm::vec3(0.0f)) {
        shape = CreateShapeFromDesc(desc.shapes[0], assetManager);
    }
    else {
        JPH::StaticCompoundShapeSettings compound;
        for (const auto& shapeDesc : desc.shapes) {
            compound.AddShape(
                JPH::Vec3(shapeDesc.offset.x, shapeDesc.offset.y, shapeDesc.offset.z),
                JPH::Quat(shapeDesc.rotation.x, shapeDesc.rotation.y, shapeDesc.rotation.z, shapeDesc.rotation.w),
                CreateShapeFromDesc(shapeDesc, assetManager)
            );
        }
        shape = compound.Create().Get();
    }

    JPH::EMotionType motionType = desc.motionType == Component::PhysicsMotionType::Static
                                      ? JPH::EMotionType::Static
                                      : desc.motionType == Component::PhysicsMotionType::Dynamic
                                            ? JPH::EMotionType::Dynamic
                                            : JPH::EMotionType::Kinematic;

    JPH::ObjectLayer layer = desc.motionType == Component::PhysicsMotionType::Static ? Physics::Layers::NON_MOVING : Physics::Layers::MOVING;

    JPH::BodyCreationSettings settings(shape, position, rotation, motionType, layer);
    if (desc.motionType == Component::PhysicsMotionType::Dynamic) {
        settings.mMassPropertiesOverride.mMass = desc.mass;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    }
    settings.mMotionQuality = desc.motionQuality;

    return bodyInterface.CreateAndAddBody(settings,
                                          desc.motionType == Component::PhysicsMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
}

JPH::ShapeRefC CreateShapeFromDesc(const Component::PhysicsShapeDesc& desc, Engine::AssetManager* assetManager)
{
    switch (desc.type) {
        case Component::PhysicsShapeType::Box:
        {
            JPH::BoxShapeSettings s(JPH::Vec3(desc.box.halfExtents.x, desc.box.halfExtents.y, desc.box.halfExtents.z));
            s.SetEmbedded();
            return s.Create().Get();
        }
        case Component::PhysicsShapeType::Sphere:
        {
            JPH::SphereShapeSettings s(desc.sphere.radius);
            s.SetEmbedded();
            return s.Create().Get();
        }
        case Component::PhysicsShapeType::Capsule:
        {
            JPH::CapsuleShapeSettings s(desc.capsule.halfHeight, desc.capsule.radius);
            s.SetEmbedded();
            return s.Create().Get();
        }
        case Component::PhysicsShapeType::ConvexHull:
        {
            if (!assetManager) { return nullptr; }
            auto* model = assetManager->GetModel(desc.meshSourceHandle);
            if (!model || !model->physicsCache) { return nullptr; }
            JPH::Array<JPH::Vec3> pts;
            pts.reserve(model->physicsCache->positions.size());
            for (const auto& p : model->physicsCache->positions)
                pts.push_back({p.x, p.y, p.z});
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
                LOG_WARN(Game, "Physics mesh cache unavailable (over vertex threshold). Removing pending tag.");
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

void ResolvePhysicsBodyCreation(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    std::vector<entt::entity> resolved;

    auto view = state->registry.view<Component::PhysicsBodyDesc, Component::PendingPhysicsBodyCreationTag>();
    for (const auto& [entity, bodyDesc] : view.each()) {
        if (state->registry.all_of<Component::PendingPhysicsMeshTag>(entity)) {
            state->bPendingModelResolve = true;
            continue;
        }

        bool bDegenerate = false;
        for (const auto& shape : bodyDesc.shapes) {
            if (shape.type != Component::PhysicsShapeType::ConvexHull && shape.type != Component::PhysicsShapeType::TriangleMesh) { continue; }
            if (!shape.meshSourceModelId.IsValid() && std::holds_alternative<std::monostate>(shape.proceduralParams)) {
                LOG_WARN(Game, "PhysicsBodyDesc has mesh shape with no mesh source, skipping body creation");
                bDegenerate = true;
                break;
            }
            if (shape.meshSourceHandle.IsValid()) {
                const Engine::StaticModel* srcModel = ctx->assetManager->GetModel(shape.meshSourceHandle);
                if (srcModel && srcModel->modelLoadState == Engine::StaticModel::ModelLoadState::Loaded
                    && (!srcModel->physicsCache || srcModel->physicsCache->positions.empty())) {
                    LOG_WARN(Game, "PhysicsBodyDesc mesh shape source is loaded but has no cached geometry (vertex count over threshold), skipping body creation");
                    bDegenerate = true;
                    break;
                }
            }
        }
        if (bDegenerate) {
            resolved.push_back(entity);
            continue;
        }

        auto* transform = state->registry.try_get<Component::TransformComponent>(entity);
        if (!transform) {
            LOG_WARN(Game, "PhysicsBodyDesc on entity without TransformComponent, skipping");
            resolved.push_back(entity);
            continue;
        }

        JPH::BodyInterface& bodyInterface = ctx->physicsSystem->GetBodyInterface();

        if (auto* existing = state->registry.try_get<Component::PhysicsBodyComponent>(entity)) {
            bodyInterface.RemoveBody(existing->bodyID);
            bodyInterface.DestroyBody(existing->bodyID);
            state->registry.remove<Component::PhysicsBodyComponent>(entity);
            state->registry.remove<Component::DynamicPhysicsBodyComponent>(entity);
        }

        JPH::Vec3 pos(transform->translation.x, transform->translation.y, transform->translation.z);
        JPH::Quat rot(transform->rotation.x, transform->rotation.y, transform->rotation.z, transform->rotation.w);
        JPH::BodyID bodyId = CreateBodyFromDesc(bodyInterface, bodyDesc, pos, rot, ctx->assetManager);
        if (!bodyId.IsInvalid()) {
            state->registry.emplace<Component::PhysicsBodyComponent>(entity, bodyId);
            if (bodyDesc.motionType == Component::PhysicsMotionType::Dynamic) {
                state->registry.emplace_or_replace<Component::DynamicPhysicsBodyComponent>(entity, transform->translation, transform->rotation);
            }
        }

        resolved.push_back(entity);
    }

    for (const auto entity : resolved) {
        state->registry.remove<Component::PendingPhysicsBodyCreationTag>(entity);
    }
}
} // Game
