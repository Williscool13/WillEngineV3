//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PHYSICS_CHARACTER_H
#define WILL_ENGINE_PHYSICS_CHARACTER_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "core/string_id.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
class AssetManager;
}

namespace Physics
{
class PhysicsSystem;
}

namespace Game
{
class PhysicsCharacter
{
public:
    void Initialize(Engine::EngineState* gameState, Engine::EngineContext* ctx, glm::vec3 spawnPosition);
    void Update(float deltaTime, const glm::vec3& moveInput, bool jumpRequested, Physics::PhysicsSystem* physicsSystem);
    void Shutdown(Physics::PhysicsSystem* physicsSystem);

    [[nodiscard]] entt::entity GetEntity() const { return entity; }
    [[nodiscard]] glm::vec3 GetPosition() const;
    [[nodiscard]] glm::vec3 GetInterpolatedPosition() const;
    [[nodiscard]] glm::vec3 GetLinearVelocity() const;
    [[nodiscard]] bool IsGrounded() const;

private:
    bool CheckGrounded(Physics::PhysicsSystem* physicsSystem) const;

    Engine::EngineState* engineState{nullptr};
    Physics::PhysicsSystem* physicsSystem{nullptr};
    entt::entity entity{entt::null};

    float sphereRadius{0.5f};

    float torqueStrength{10.0f};
    float rollSpeedMultiplier{600.0f};
    float maxAngularSpeed{20.0f};
    float jumpImpulse{5.0f};

    bool grounded{false};
};
} // Game

#endif //WILL_ENGINE_PHYSICS_CHARACTER_H
