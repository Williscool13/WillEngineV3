//
// Created by William on 2025-12-21.
//

#ifndef WILL_ENGINE_FREE_CAMERA_COMPONENT_H
#define WILL_ENGINE_FREE_CAMERA_COMPONENT_H

namespace Core
{
struct EngineContext;
}

namespace Engine
{
struct GameState;
}

namespace Game
{
struct FreeCameraComponent {
    float moveSpeed = 5.0f;
    float lookSpeed = 0.1f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

void UpdateFreeCamera(Core::EngineContext* ctx, Engine::GameState* state);
} // Game

#endif //WILL_ENGINE_FREE_CAMERA_COMPONENT_H