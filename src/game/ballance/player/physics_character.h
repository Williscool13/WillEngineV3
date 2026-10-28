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
class SystemRegistry;
}

namespace Physics
{
class PhysicsWorld;
}

namespace Ballance
{
class PhysicsCharacter
{
public:
    void Initialize(Engine::EngineState* gameState, Engine::EngineContext* ctx, glm::vec3 spawnPosition, Engine::SystemRegistry& registry);
    void Update(float deltaTime, const glm::vec3& moveInput, bool jumpRequested, const Physics::PhysicsWorld* physicsWorld, Engine::SystemRegistry& registry);
    void Shutdown(const Physics::PhysicsWorld* physicsWorld, Engine::SystemRegistry& registry);

    [[nodiscard]] entt::entity GetEntity() const { return entity; }
    [[nodiscard]] glm::vec3 GetPosition(Engine::SystemRegistry& registry) const;
    [[nodiscard]] glm::vec3 GetInterpolatedPosition(Engine::SystemRegistry& registry) const;
    [[nodiscard]] glm::vec3 GetLinearVelocity(Engine::SystemRegistry& registry) const;
    [[nodiscard]] bool IsGrounded() const;

private:
    bool CheckGrounded(const Physics::PhysicsWorld* physicsWorld, Engine::SystemRegistry& registry) const;

    Engine::EngineState* engineState{nullptr};
    const Physics::PhysicsWorld* physicsWorld{nullptr};
    entt::entity entity{entt::null};

    float sphereRadius{0.5f};

    float torqueStrength{10.0f};
    float rollSpeedMultiplier{600.0f};
    float maxAngularSpeed{20.0f};
    float jumpImpulse{100.0f};

    bool grounded{false};
};
} // Ballance

#endif //WILL_ENGINE_PHYSICS_CHARACTER_H
