//
// Created by William on 2025-12-25.
//

#include "physics_system.h"
#include "physics/physics_system.h"

#include "core/include/engine_context.h"
#include "core/time/time_frame.h"
#include "game/fwd_components.h"
#include "engine/engine_api.h"

namespace Game::System
{
void UpdatePhysics(Core::EngineContext* ctx, Engine::GameState* state)
{
    auto* physics = ctx->physicsSystem;
    state->physicsDeltaTimeAccumulator += state->timeFrame->deltaTime;

    while (state->physicsDeltaTimeAccumulator >= Physics::PHYSICS_TIMESTEP) {
        auto view = state->registry.view<PhysicsBodyComponent, TransformComponent>();
        for (auto [entity, physicsBody, transform] : view.each()) {
            auto& bodyInterface = physics->GetBodyInterface();
            JPH::RVec3 pos = bodyInterface.GetPosition(physicsBody.bodyID);
            JPH::Quat rot = bodyInterface.GetRotation(physicsBody.bodyID);

            physicsBody.previousPosition = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
            physicsBody.previousRotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
        }

        physics->Step(Physics::PHYSICS_TIMESTEP);


        for (auto [entity, physicsBody, transform] : view.each()) {
            auto& bodyInterface = physics->GetBodyInterface();
            JPH::RVec3 pos = bodyInterface.GetPosition(physicsBody.bodyID);
            JPH::Quat rot = bodyInterface.GetRotation(physicsBody.bodyID);

            transform.transform.translation = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
            transform.transform.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
        }

        state->physicsDeltaTimeAccumulator -= Physics::PHYSICS_TIMESTEP;
    }

    state->physicsInterpolationAlpha = state->physicsDeltaTimeAccumulator / Physics::PHYSICS_TIMESTEP;
}
} // Game
