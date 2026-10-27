//
// Created by William on 2026-03-21.
//

#ifndef WILL_ENGINE_COMMON_SYSTEMS_H
#define WILL_ENGINE_COMMON_SYSTEMS_H

#include "engine/core/registry.h"

namespace Engine
{
void ConnectCommonObservers(Engine::Registry& registry);
void DisconnectCommonObservers(Engine::Registry& registry);
} // Engine

#endif //WILL_ENGINE_COMMON_SYSTEMS_H