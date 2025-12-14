//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_TIME_MANAGER_H
#define WILL_ENGINE_TIME_MANAGER_H

#include <chrono>
#include "time_frame.h"

namespace Core
{
class TimeManager
{
public:
    TimeManager();

    void Reset();
    void Update();

    const TimeFrame& GetTime() const { return currentTime; }

private:
    TimeFrame currentTime{};
    std::chrono::time_point<std::chrono::steady_clock> startTime;
    std::chrono::time_point<std::chrono::steady_clock> lastTime;
};
} // Core

#endif //WILL_ENGINE_TIME_MANAGER_H