//
// Created by William on 2026-03-20.
//

#include "character.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include "game/components/character_components.h"
#include "game/components/common_components.h"
#include "game/components/core_components.h"
#include "game/components/render_components.h"
#include "game/systems/scene_system.h"
#include "physics/physics_system.h"

namespace Game
{
void Character::Initialize(Engine::GameState* gameState, Physics::PhysicsSystem* physicsSystem, glm::vec3 spawnPosition)
{
    engineGameState = gameState;

    entity = CreateSceneEntity(engineGameState);
    auto transformCreated =  CreateComponent<Component::TransformComponent>(engineGameState, entity);
    assert(transformCreated && "Failed to make transform for Character::Initialize.");

    auto& transform = engineGameState->registry.get<Component::TransformComponent>(entity);
    transform.translation = spawnPosition;
    engineGameState->registry.emplace_or_replace<Component::DirtyTransformTag>(entity);
    engineGameState->registry.emplace<Component::DoNotSerializeTag>(entity);

    JPH::ShapeRefC capsuleShape = JPH::CapsuleShapeSettings(capsuleHalfHeight, capsuleRadius).Create().Get();
    JPH::ShapeRefC standingShape = JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3(0, capsuleHalfHeight + capsuleRadius, 0),
        JPH::Quat::sIdentity(),
        capsuleShape
    ).Create().Get();

    JPH::CharacterVirtualSettings settings;
    settings.mShape = standingShape;
    settings.mInnerBodyShape = standingShape;
    settings.mInnerBodyLayer = Physics::Layers::MOVING;
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(maxSlopeAngle);
    settings.mMass = 70.0f;
    settings.mMaxStrength = 100.0f;

    JPH::RVec3 joltPos(spawnPosition.x, spawnPosition.y, spawnPosition.z);
    auto* characterPhysics = new JPH::CharacterVirtual(&settings, joltPos, JPH::Quat::sIdentity(), 0, &physicsSystem->GetPhysicsSystem());

    bool createdPhysicsComp = CreateComponent<Component::CharacterPhysicsComponent>(engineGameState, entity);
    assert(createdPhysicsComp && "Failed to create character physics comp.");
    auto& characterPhysicsComp = engineGameState->registry.get<Component::CharacterPhysicsComponent>(entity);
    characterPhysicsComp.character = characterPhysics;
    characterPhysicsComp.capsuleShape = capsuleShape;
    characterPhysicsComp.standingShape = standingShape;

    bool createdProceduralMesh = CreateComponent<Component::ProceduralMeshComponent>(engineGameState, entity);
    assert(createdProceduralMesh && "Failed to create procedural mesh for Player Character.");

    auto& playerMesh = engineGameState->registry.get<Component::ProceduralMeshComponent>(entity);
    playerMesh.params = Engine::DodecahedronParams{
        .radius = capsuleHalfHeight + capsuleRadius,
    };
    playerMesh.renderOffset = glm::vec3(0.0f, capsuleHalfHeight + capsuleRadius, 0.0f);
    Component::RecreateProceduralMesh(playerMesh, engineGameState->registry, entity);
}

void Character::Update(float deltaTime, const glm::vec3& moveInput, bool jumpRequested, Physics::PhysicsSystem* physicsSystem)
{
    auto& comp = engineGameState->registry.get<Component::CharacterPhysicsComponent>(entity);
    JPH::CharacterVirtual* character = comp.character.GetPtr();

    JPH::Vec3 currentVelocity = character->GetLinearVelocity();
    JPH::Vec3 desiredVelocity(moveInput.x * moveSpeed, 0.0f, moveInput.z * moveSpeed);

    JPH::Vec3 newVelocity{};
    if (character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround) {
        newVelocity = desiredVelocity;

        if (jumpRequested) {
            newVelocity += JPH::Vec3(0, jumpSpeed, 0);
        }
    }
    else {
        // Airborne: keep horizontal from input, apply gravity
        newVelocity = JPH::Vec3(desiredVelocity.GetX(), currentVelocity.GetY(), desiredVelocity.GetZ());
    }

    // Gravity
    JPH::Vec3 gravity = physicsSystem->GetPhysicsSystem().GetGravity();
    newVelocity += gravity * deltaTime;

    character->SetLinearVelocity(newVelocity);

    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    character->ExtendedUpdate(
        deltaTime,
        gravity,
        updateSettings,
        physicsSystem->GetPhysicsSystem().GetDefaultBroadPhaseLayerFilter(Physics::Layers::MOVING),
        physicsSystem->GetPhysicsSystem().GetDefaultLayerFilter(Physics::Layers::MOVING),
        {},
        {},
        physicsSystem->GetTempAllocator()
    );
}

void Character::Shutdown(Physics::PhysicsSystem* physicsSystem)
{
    if (engineGameState && engineGameState->registry.valid(entity)) {
        engineGameState->registry.remove<Component::CharacterPhysicsComponent>(entity);
        engineGameState->registry.destroy(entity);
    }
    entity = entt::null;
    engineGameState = nullptr;
}

glm::vec3 Character::GetPosition() const
{
    if (!engineGameState || !engineGameState->registry.valid(entity)) return {};
    return engineGameState->registry.get<Component::TransformComponent>(entity).translation;
}

glm::vec3 Character::GetLinearVelocity() const
{
    auto& comp = engineGameState->registry.get<Component::CharacterPhysicsComponent>(entity);
    JPH::Vec3 v = comp.character->GetLinearVelocity();
    return {v.GetX(), v.GetY(), v.GetZ()};
}

bool Character::IsGrounded() const
{
    auto& comp = engineGameState->registry.get<Component::CharacterPhysicsComponent>(entity);
    return comp.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}
} // Game
