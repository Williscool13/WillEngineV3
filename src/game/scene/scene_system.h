//
// Created by William on 2026-02-26.
//

#ifndef WILL_ENGINE_SCENE_SYSTEM_H
#define WILL_ENGINE_SCENE_SYSTEM_H
#include <entt/entt.hpp>

#include "scene.h"

namespace Game::System
{
Scene SaveScene(entt::registry registry);
void LoadScene(Scene& scene);
} // Game

#endif //WILL_ENGINE_SCENE_SYSTEM_H