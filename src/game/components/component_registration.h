//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_GAME_COMPONENT_REGISTRATION_H
#define WILL_ENGINE_GAME_COMPONENT_REGISTRATION_H

namespace Engine
{
struct ComponentRegistry;
}

namespace Game
{
void RegisterGameComponents(Engine::ComponentRegistry& componentRegistry);
} // Game

#endif //WILL_ENGINE_GAME_COMPONENT_REGISTRATION_H
