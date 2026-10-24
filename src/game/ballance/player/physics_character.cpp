//
// Created by William on 2026-03-21.
//

#include "physics_character.h"

#include "engine/include/engine_context.h"
#include "engine/asset_manager.h"
#include "engine/components/common_components.h"
#include "engine/components/core_components.h"
#include "engine/components/physics/physics_components.h"
#include "engine/components/physics/physics_body_component.h"
#include "game/ballance/fwd_components.h"
#include "game/logging/game_log.h"
#include "engine/systems/scene_system.h"
#include "physics/physics_world.h"

namespace Ballance
{
void PhysicsCharacter::Initialize(Engine::EngineState* gameState, Engine::EngineContext* ctx, glm::vec3 spawnPosition)
{
    engineState = gameState;
    physicsWorld = ctx->physicsWorld;

    constexpr StringID WOODEN_BALL_PREFAB_ID{4933586796549546436};
    entity = Engine::SpawnPrefab(engineState, ctx->assetManager, WOODEN_BALL_PREFAB_ID);
    assert(entity != entt::null && "Failed to spawn player character prefab.");

    engineState->registry.emplace<Component::DoNotSerializeTag>(entity);

    auto& transform = engineState->registry.get<Component::TransformComponent>(entity);
    transform.translation = spawnPosition;
    engineState->registry.emplace_or_replace<Component::DirtyTransformRequest>(entity);
}

void PhysicsCharacter::Update(float deltaTime, const glm::vec3& moveInput, bool jumpRequested, Physics::PhysicsWorld* inPhysicsWorld)
{
    auto* physicsBody = engineState->registry.try_get<Component::PhysicsBodyRuntime>(entity);
    if (!physicsBody) return;
    const auto bodyId = physicsBody->bodyID;

    grounded = CheckGrounded(inPhysicsWorld);

    if (glm::length(moveInput) > 0.001f) {
        // cross(up, moveDir) gives the torque axis that rolls the ball in moveDir
        const float rollTorque = torqueStrength * rollSpeedMultiplier * deltaTime;
        Vec3 torque(moveInput.z * rollTorque, 0.0f, -moveInput.x * rollTorque);

        const Vec3 angVel = inPhysicsWorld->GetAngularVelocity(bodyId);
        float angSpeedSq = glm::dot(angVel, angVel);
        if (angSpeedSq > maxAngularSpeed * maxAngularSpeed * 0.64f) {
            float ratio = 1.0f - (angSpeedSq / (maxAngularSpeed * maxAngularSpeed));
            if (ratio < 0.1f) ratio = 0.1f;
            torque *= ratio;
        }

        inPhysicsWorld->AddTorque(bodyId, torque);
    }

    if (jumpRequested && grounded) {
        inPhysicsWorld->AddImpulse(bodyId, Vec3(0.0f, jumpImpulse, 0.0f));
        WLOG_INFO(Player, "Jump");
    }
}

void PhysicsCharacter::Shutdown(Physics::PhysicsWorld* inPhysicsWorld)
{
    if (engineState && engineState->registry.valid(entity)) {
        engineState->registry.destroy(entity);
    }
    entity = entt::null;
    engineState = nullptr;
    physicsWorld = nullptr;
}

glm::vec3 PhysicsCharacter::GetPosition() const
{
    if (!engineState || !engineState->registry.valid(entity)) return {};
    return engineState->registry.get<Component::TransformComponent>(entity).translation;
}

glm::vec3 PhysicsCharacter::GetInterpolatedPosition() const
{
    if (!engineState || !engineState->registry.valid(entity)) return {};
    auto* dynamic = engineState->registry.try_get<Component::DynamicPhysicsBodyRuntime>(entity);
    if (!dynamic) return GetPosition();
    float alpha = engineState->physics.interpolationAlpha;
    return glm::mix(dynamic->previousPosition, dynamic->currentPosition, alpha);
}

glm::vec3 PhysicsCharacter::GetLinearVelocity() const
{
    if (!physicsWorld) return {};
    auto* physicsBody = engineState->registry.try_get<Component::PhysicsBodyRuntime>(entity);
    if (!physicsBody) return {};
    return physicsWorld->GetLinearVelocity(physicsBody->bodyID);
}

bool PhysicsCharacter::IsGrounded() const
{
    return grounded;
}

bool PhysicsCharacter::CheckGrounded(Physics::PhysicsWorld* inPhysicsWorld) const
{
    auto* physicsBody = engineState->registry.try_get<Component::PhysicsBodyRuntime>(entity);
    if (!physicsBody) return false;

    // Cast ray downward from center of sphere, slightly past the bottom
    Physics::CastHit hit;
    const Vec3 bodyPos = inPhysicsWorld->GetPosition(physicsBody->bodyID);
    return inPhysicsWorld->CastRay(bodyPos, Vec3(0.0f, -(sphereRadius + 0.15f), 0.0f), hit, physicsBody->bodyID);
}
} // Ballance
