//
// Created by William on 2026-03-20.
//

#ifndef WILL_ENGINE_CHARACTER_H
#define WILL_ENGINE_CHARACTER_H

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Engine
{
struct GameState;
}

namespace Core
{
struct InputFrame;
}

namespace Physics
{
class PhysicsSystem;
}

namespace Game
{
class Character
{
public:
    void Initialize(Engine::GameState* gameState, Physics::PhysicsSystem* physicsSystem, glm::vec3 spawnPosition);
    void Update(float deltaTime, const glm::vec3& moveInput, bool jumpRequested, Physics::PhysicsSystem* physicsSystem);
    void Shutdown(Physics::PhysicsSystem* physicsSystem);

    [[nodiscard]] entt::entity GetEntity() const { return entity; }
    [[nodiscard]] glm::vec3 GetPosition() const;
    [[nodiscard]] glm::vec3 GetLinearVelocity() const;
    [[nodiscard]] bool IsGrounded() const;

private:
    Engine::GameState* engineGameState{nullptr};
    entt::entity entity{entt::null};

    float moveSpeed{5.0f};
    float jumpSpeed{8.0f};
    float maxSlopeAngle{50.0f};

    // Capsule shape
    float capsuleRadius{0.3f};
    float capsuleHalfHeight{0.5f};
};
} // Game

#endif //WILL_ENGINE_CHARACTER_H
