//
// Created by William on 2026-03-21.
//

#include "physics_character.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>

#include "core/include/engine_context.h"
#include "engine/asset_manager.h"
#include "game/components/common_components.h"
#include "game/components/core_components.h"
#include "../../components/physics/physics_components.h"
#include "game/components/physics/physics_body_component.h"
#include "game/systems/scene_system.h"
#include "physics/physics_system.h"

namespace Game
{
void PhysicsCharacter::Initialize(Engine::GameState* gameState, Core::EngineContext* ctx, glm::vec3 spawnPosition)
{
    engineGameState = gameState;
    physicsSystem = ctx->physicsSystem;

    constexpr StringID WOODEN_BALL_PREFAB_ID{4933586796549546436};
    entity = SpawnPrefab(engineGameState, ctx->assetManager, WOODEN_BALL_PREFAB_ID);
    assert(entity != entt::null && "Failed to spawn player character prefab.");

    engineGameState->registry.emplace<Component::DoNotSerializeTag>(entity);

    auto& transform = engineGameState->registry.get<Component::TransformComponent>(entity);
    transform.translation = spawnPosition;
    engineGameState->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
}

void PhysicsCharacter::Update(float deltaTime, const glm::vec3& moveInput, bool jumpRequested, Physics::PhysicsSystem* inPhysicsSystem)
{
    auto* physicsBody = engineGameState->registry.try_get<Component::PhysicsBodyComponent>(entity);
    if (!physicsBody) return;

    auto& bodyInterface = inPhysicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = physicsBody->bodyID;

    grounded = CheckGrounded(inPhysicsSystem);

    if (glm::length(moveInput) > 0.001f) {
        // cross(up, moveDir) gives the torque axis that rolls the ball in moveDir
        JPH::Vec3 torque(moveInput.z * torqueStrength, 0.0f, -moveInput.x * torqueStrength);

        JPH::Vec3 angVel = bodyInterface.GetAngularVelocity(bodyId);
        float angSpeedSq = angVel.LengthSq();
        if (angSpeedSq > maxAngularSpeed * maxAngularSpeed * 0.64f) {
            float ratio = 1.0f - (angSpeedSq / (maxAngularSpeed * maxAngularSpeed));
            if (ratio < 0.1f) ratio = 0.1f;
            torque *= ratio;
        }

        bodyInterface.AddTorque(bodyId, torque);
    }

    if (jumpRequested && grounded) {
        bodyInterface.AddImpulse(bodyId, JPH::Vec3(0.0f, jumpImpulse, 0.0f));
    }
}

void PhysicsCharacter::Shutdown(Physics::PhysicsSystem* inPhysicsSystem)
{
    if (engineGameState && engineGameState->registry.valid(entity)) {
        engineGameState->registry.destroy(entity);
    }
    entity = entt::null;
    engineGameState = nullptr;
    physicsSystem = nullptr;
}

glm::vec3 PhysicsCharacter::GetPosition() const
{
    if (!engineGameState || !engineGameState->registry.valid(entity)) return {};
    return engineGameState->registry.get<Component::TransformComponent>(entity).translation;
}

glm::vec3 PhysicsCharacter::GetInterpolatedPosition() const
{
    if (!engineGameState || !engineGameState->registry.valid(entity)) return {};
    auto* dynamic = engineGameState->registry.try_get<Component::DynamicPhysicsBodyComponent>(entity);
    if (!dynamic) return GetPosition();
    const auto& transform = engineGameState->registry.get<Component::TransformComponent>(entity);
    float alpha = engineGameState->physicsInterpolationAlpha;
    return glm::mix(dynamic->previousPosition, transform.translation, alpha);
}

glm::vec3 PhysicsCharacter::GetLinearVelocity() const
{
    if (!physicsSystem) return {};
    auto* physicsBody = engineGameState->registry.try_get<Component::PhysicsBodyComponent>(entity);
    if (!physicsBody) return {};
    JPH::Vec3 v = physicsSystem->GetBodyInterface().GetLinearVelocity(physicsBody->bodyID);
    return {v.GetX(), v.GetY(), v.GetZ()};
}

bool PhysicsCharacter::IsGrounded() const
{
    return grounded;
}

bool PhysicsCharacter::CheckGrounded(Physics::PhysicsSystem* inPhysicsSystem) const
{
    auto* physicsBody = engineGameState->registry.try_get<Component::PhysicsBodyComponent>(entity);
    if (!physicsBody) return false;

    JPH::RVec3 bodyPos = inPhysicsSystem->GetBodyInterface().GetPosition(physicsBody->bodyID);

    // Cast ray downward from center of sphere, slightly past the bottom
    JPH::RRayCast ray;
    ray.mOrigin = bodyPos;
    ray.mDirection = JPH::Vec3(0.0f, -(sphereRadius + 0.15f), 0.0f);

    JPH::RayCastResult hit;
    JPH::IgnoreSingleBodyFilter bodyFilter(physicsBody->bodyID);

    return inPhysicsSystem->GetPhysicsSystem().GetNarrowPhaseQuery().CastRay(
        ray,
        hit,
        {},
        {},
        bodyFilter
    );
}
} // Game
