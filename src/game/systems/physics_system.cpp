//
// Created by William on 2025-12-25.
//

#include "physics_system.h"
#include "physics/physics_system.h"

#include "core/include/engine_context.h"
#include "core/time/time_frame.h"
#include "game/fwd_components.h"
#include "engine/engine_api.h"
#include "game/components/physics/dynamic_physics_body_component.h"

namespace Game::System
{
void UpdatePhysics(Core::EngineContext* ctx, Engine::GameState* state)
{
    auto* physics = ctx->physicsSystem;
    state->physicsDeltaTimeAccumulator += state->timeFrame->deltaTime;

    while (state->physicsDeltaTimeAccumulator >= Physics::PHYSICS_TIMESTEP) {
        auto& bodyInterface = physics->GetBodyInterface();

        auto view = state->registry.view<DynamicPhysicsBodyComponent, PhysicsBodyComponent, TransformComponent>();

        for (auto [entity, dynamic, physicsBody, transform] : view.each()) {
            if (state->registry.all_of<DirtyPhysicsTransformComponent>(entity)) {
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

        state->registry.clear<DirtyPhysicsTransformComponent>();
        physics->Step(Physics::PHYSICS_TIMESTEP);

        for (auto [entity, dynamic, physicsBody, transform] : view.each()) {
            JPH::RVec3 pos = bodyInterface.GetPosition(physicsBody.bodyID);
            JPH::Quat rot = bodyInterface.GetRotation(physicsBody.bodyID);
            transform.translation = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
            transform.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
        }

        state->physicsDeltaTimeAccumulator -= Physics::PHYSICS_TIMESTEP;
    }

    state->physicsInterpolationAlpha = state->physicsDeltaTimeAccumulator / Physics::PHYSICS_TIMESTEP;
}
} // Game
