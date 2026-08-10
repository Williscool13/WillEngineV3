//
// Created by William on 2026-03-20.
//

#ifndef WILL_ENGINE_PLAYER_CONTROLLER_H
#define WILL_ENGINE_PLAYER_CONTROLLER_H

#include <optional>

#include "character.h"
#include "game/gameplay/camera/gameplay_camera.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game
{
class PlayerController
{
public:
    void Initialize(Engine::EngineState* gameState, Physics::PhysicsSystem* physicsSystem, glm::vec3 spawnPosition);
    void Update(Engine::EngineContext* ctx, Engine::EngineState* state);
    void Shutdown(Physics::PhysicsSystem* physicsSystem);

    Character* GetCharacter() { return character ? &*character : nullptr; }

private:
    std::optional<Character> character;

    float lookYaw{0.0f};
    float lookPitch{0.0f};
    float lookSpeed{0.1f};
    float characterYaw{0.0f};

    Camera::OrbitCameraParams cameraParams{};
    Camera::OrbitCameraState cameraState{};
};
} // Game

#endif //WILL_ENGINE_PLAYER_CONTROLLER_H
