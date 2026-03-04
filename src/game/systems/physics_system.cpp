//
// Created by William on 2025-12-25.
//

#include "physics_system.h"
#include "physics/physics_system.h"

#include <tracy/Tracy.hpp>

#include "core/include/engine_context.h"
#include "core/time/time_frame.h"
#include "game/fwd_components.h"
#include "engine/engine_api.h"
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
        for (auto [entity, physicsBody, transform] : teleportView.each()) {
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
        for (auto [entity, physicsBody, transform] : kinematicView.each()) {
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
            state->registry.emplace_or_replace<Component::DirtyRenderTransformComponent>(entity);

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
    auto& filter = ctx->physicsSystem->GetDebugDrawFilter();
    filter.Clear();

    auto view = state->registry.view<Component::DrawPhysicsDebugTag, Component::PhysicsBodyComponent>();
    for (auto [entity, physicsBody] : view.each()) {
        filter.AddBody(physicsBody.bodyID);
    }

    ctx->physicsSystem->DrawDebug(&frameBuffer->mainViewFamily);
#endif
}

JPH::BodyID CreateBodyFromDesc(JPH::BodyInterface& bodyInterface, const Component::PhysicsBodyDesc& desc, JPH::RVec3 position, JPH::Quat rotation)
{
    JPH::ShapeRefC shape;

    if (desc.shapes.empty()) {
        return JPH::BodyID(JPH::BodyID::cInvalidBodyID);
    }
    if (desc.shapes.size() == 1 && desc.shapes[0].offset == glm::vec3(0.0f)) {
        shape = CreateShapeFromDesc(desc.shapes[0]);
    }
    else {
        JPH::StaticCompoundShapeSettings compound;
        for (const auto& shapeDesc : desc.shapes) {
            compound.AddShape(
                JPH::Vec3(shapeDesc.offset.x, shapeDesc.offset.y, shapeDesc.offset.z),
                JPH::Quat(shapeDesc.rotation.x, shapeDesc.rotation.y, shapeDesc.rotation.z, shapeDesc.rotation.w),
                CreateShapeFromDesc(shapeDesc)
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

JPH::ShapeRefC CreateShapeFromDesc(const Component::PhysicsShapeDesc& desc)
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
    }
    return nullptr;
}
} // Game
