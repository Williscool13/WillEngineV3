//
// Created by William on 2026-03-20.
//

#ifndef WILL_ENGINE_PLAYER_CONTROLLER_H
#define WILL_ENGINE_PLAYER_CONTROLLER_H

#include <memory>

#include "character.h"

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
class PlayerController
{
public:
    void Initialize(Engine::GameState* gameState, Physics::PhysicsSystem* physicsSystem, glm::vec3 spawnPosition);
    void Update(Core::EngineContext* ctx, Engine::GameState* state);
    void Shutdown(Physics::PhysicsSystem* physicsSystem);

    Character* GetCharacter() { return character.get(); }

private:
    std::unique_ptr<Character> character;

    float lookYaw{0.0f};
    float lookPitch{0.0f};
    float lookSpeed{0.1f};
    float characterYaw{0.0f};

    float orbitDistance{3.5f};
    float orbitSideOffset{0.5f};
};
} // Game

#endif //WILL_ENGINE_PLAYER_CONTROLLER_H
