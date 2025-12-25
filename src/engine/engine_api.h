//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_ENGINE_API_H
#define WILL_ENGINE_ENGINE_API_H

#include "entt/entt.hpp"

namespace Core
{
struct TimeFrame;
struct InputFrame;
}

namespace Engine
{
struct GameState
{
    const Core::InputFrame* inputFrame{nullptr};
    const Core::TimeFrame* timeFrame{nullptr};
    entt::registry registry;

    // Physics
    float physicsDeltaTimeAccumulator = 0.0f;
};

class EngineAPI
{};
} // Engine

#endif //WILL_ENGINE_ENGINE_API_H
