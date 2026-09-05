//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_COMMON_SYSTEMS_H
#define WILL_ENGINE_COMMON_SYSTEMS_H

#include <entt/entt.hpp>

namespace Engine
{
void ConnectCommonObservers(entt::registry& registry);
void DisconnectCommonObservers(entt::registry& registry);
} // Engine

#endif //WILL_ENGINE_COMMON_SYSTEMS_H