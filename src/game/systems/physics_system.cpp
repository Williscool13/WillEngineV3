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

namespace Game::System
{
void UpdatePhysics(Core::EngineContext* ctx, Engine::GameState* state)
{
    ZoneScoped;
    auto* physics = ctx->physicsSystem;
    state->physicsDeltaTimeAccumulator += state->timeFrame->deltaTime;

    while (state->physicsDeltaTimeAccumulator >= Physics::PHYSICS_TIMESTEP) {
        auto& bodyInterface = physics->GetBodyInterface();

        auto view = state->registry.view<Component::DynamicPhysicsBodyComponent, Component::PhysicsBodyComponent, Component::TransformComponent>();

        for (auto [entity, dynamic, physicsBody, transform] : view.each()) {
            if (state->registry.all_of<Component::DirtyPhysicsTransformComponent>(entity)) {
                bodyInterface.SetPositionAndRotation(
                    physicsBody.bodyID,
                    JPH::RVec3(transform.translation.x, transform.translation.y, transform.translation.z),
                    JPH::Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w),
                    JPH::EActivation::Activate
                );
            }

            dynamic.previousPosition = transform.translation;
            dynamic.previousRotation = transform.rotation;
        }

        state->registry.clear<Component::DirtyPhysicsTransformComponent>();
        physics->Step(Physics::PHYSICS_TIMESTEP);

        for (auto [entity, dynamic, physicsBody, transform] : view.each()) {
            JPH::RVec3 pos = bodyInterface.GetPosition(physicsBody.bodyID);
            JPH::Quat rot = bodyInterface.GetRotation(physicsBody.bodyID);

            transform.translation = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
            transform.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
            state->registry.emplace_or_replace<Component::DirtyRenderTransformTag>(entity);

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

    auto view = state->registry.view<Component::DrawPhysicsDebugComponent, Component::PhysicsBodyComponent>();
    for (auto [entity, physicsBody] : view.each()) {
        filter.AddBody(physicsBody.bodyID);
    }

    ctx->physicsSystem->DrawDebug(&frameBuffer->mainViewFamily);
#endif
}
} // Game
