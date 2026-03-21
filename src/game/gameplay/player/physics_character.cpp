//
// Created by William on 2026-03-21.
//

#include "physics_character.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>

#include "game/components/common_components.h"
#include "game/components/core_components.h"
#include "game/components/physics_components.h"
#include "game/components/render_components.h"
#include "game/systems/scene_system.h"
#include "game/systems/physics_system.h"
#include "physics/physics_system.h"
#include "physics/layers/layer_interface.h"

namespace Game
{
void PhysicsCharacter::Initialize(Engine::GameState* gameState, Physics::PhysicsSystem* inPhysicsSystem, glm::vec3 spawnPosition)
{
    engineGameState = gameState;
    physicsSystem = inPhysicsSystem;

    entity = CreateSceneEntity(engineGameState);
    bool transformCreated = CreateComponent<Component::TransformComponent>(engineGameState, entity);
    assert(transformCreated && "Failed to make transform for PhysicsCharacter::Initialize.");

    auto& transform = engineGameState->registry.get<Component::TransformComponent>(entity);
    transform.translation = spawnPosition;
    engineGameState->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
    engineGameState->registry.emplace<Component::DoNotSerializeTag>(entity);

    // Sphere rigid body
    JPH::SphereShapeSettings shapeSettings(sphereRadius);
    JPH::ShapeRefC shape = shapeSettings.Create().Get();

    Component::PhysicsBodyDesc bodyDesc{};
    bodyDesc.motionType = Component::PhysicsMotionType::Dynamic;
    bodyDesc.mass = 5.0f;
    bodyDesc.friction = 0.6f;
    bodyDesc.motionQuality = JPH::EMotionQuality::LinearCast;
    bodyDesc.shapeRef = shape;

    JPH::RVec3 joltPos(spawnPosition.x, spawnPosition.y + sphereRadius, spawnPosition.z);
    JPH::BodyID bodyId = CreateBodyFromShape(physicsSystem->GetBodyInterface(), bodyDesc, joltPos, JPH::Quat::sIdentity(), Physics::Layers::PLAYER);
    assert(!bodyId.IsInvalid() && "Failed to create physics body for PhysicsCharacter.");

    physicsSystem->GetBodyInterface().SetRestitution(bodyId, 0.2f);

    engineGameState->registry.emplace<Component::PhysicsBodyComponent>(entity, bodyId);
    engineGameState->registry.emplace<Component::DynamicPhysicsBodyComponent>(entity, spawnPosition, glm::quat(1, 0, 0, 0));

    // Procedural sphere mesh
    bool createdProceduralMesh = CreateComponent<Component::ProceduralMeshComponent>(engineGameState, entity);
    assert(createdProceduralMesh && "Failed to create procedural mesh for PhysicsCharacter.");

    auto& mesh = engineGameState->registry.get<Component::ProceduralMeshComponent>(entity);
    mesh.params = Engine::SubdividedSphereParams{
        .radius = sphereRadius,
        .subdivisions = 3,
    };
    Component::RecreateProceduralMesh(mesh, engineGameState->registry, entity);
}

void PhysicsCharacter::Update(float deltaTime, const glm::vec3& moveInput, bool jumpRequested, Physics::PhysicsSystem* inPhysicsSystem)
{
    auto* physicsBody = engineGameState->registry.try_get<Component::PhysicsBodyComponent>(entity);
    if (!physicsBody) return;

    auto& bodyInterface = inPhysicsSystem->GetBodyInterface();
    JPH::BodyID bodyId = physicsBody->bodyID;

    grounded = CheckGrounded(inPhysicsSystem);

    JPH::Vec3 currentVelocity = bodyInterface.GetLinearVelocity(bodyId);
    float horizontalSpeedSq = currentVelocity.GetX() * currentVelocity.GetX() + currentVelocity.GetZ() * currentVelocity.GetZ();

    if (glm::length(moveInput) > 0.001f) {
        JPH::Vec3 force(moveInput.x * moveForce, 0.0f, moveInput.z * moveForce);

        // Taper force near max speed
        if (horizontalSpeedSq > maxSpeed * maxSpeed * 0.64f) {
            float ratio = 1.0f - (horizontalSpeedSq / (maxSpeed * maxSpeed));
            if (ratio < 0.1f) ratio = 0.1f;
            force *= ratio;
        }

        if (!grounded) {
            force *= 0.3f;
        }

        bodyInterface.AddForce(bodyId, force);
    }
    else if (grounded) {
        // Ground drag when no input
        JPH::Vec3 drag(-currentVelocity.GetX() * 3.0f, 0.0f, -currentVelocity.GetZ() * 3.0f);
        bodyInterface.AddForce(bodyId, drag);
    }

    if (jumpRequested && grounded) {
        bodyInterface.AddImpulse(bodyId, JPH::Vec3(0.0f, jumpImpulse, 0.0f));
    }
}

void PhysicsCharacter::Shutdown(Physics::PhysicsSystem* inPhysicsSystem)
{
    if (engineGameState && engineGameState->registry.valid(entity)) {
        auto* physicsBody = engineGameState->registry.try_get<Component::PhysicsBodyComponent>(entity);
        if (physicsBody) {
            inPhysicsSystem->GetBodyInterface().RemoveBody(physicsBody->bodyID);
            inPhysicsSystem->GetBodyInterface().DestroyBody(physicsBody->bodyID);
            engineGameState->registry.remove<Component::PhysicsBodyComponent>(entity);
            engineGameState->registry.remove<Component::DynamicPhysicsBodyComponent>(entity);
        }
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
