//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_PHYSICS_PLAYER_CONTROLLER_H
#define WILL_ENGINE_PHYSICS_PLAYER_CONTROLLER_H

#include <memory>

#include "physics_character.h"
#include "game/gameplay/camera/gameplay_camera.h"

namespace Engine
{
struct EngineContext;
struct EngineState;
}

namespace Game
{
class PhysicsPlayerController
{
public:
    void Initialize(Engine::EngineState* gameState, Engine::EngineContext* ctx, glm::vec3 spawnPosition);
    void Update(Engine::EngineContext* ctx, Engine::EngineState* state);
    void Shutdown(Physics::PhysicsSystem* physicsSystem);

    PhysicsCharacter* GetCharacter() { return character.get(); }

private:
    std::unique_ptr<PhysicsCharacter> character;

    float lookYaw{0.0f};
    float lookPitch{0.0f};
    float lookSpeed{0.1f};

    Camera::OrbitCameraParams cameraParams{};
    Camera::OrbitCameraState cameraState{};
};
} // Game

#endif //WILL_ENGINE_PHYSICS_PLAYER_CONTROLLER_H
