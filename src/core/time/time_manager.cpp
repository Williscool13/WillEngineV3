//
// Created by William on 2025-12-14.
//

#include "time_manager.h"

namespace Core
{
TimeManager::TimeManager()
{
    startTime = std::chrono::steady_clock::now();
    lastTime = startTime;
}

void TimeManager::Reset()
{
    startTime = std::chrono::steady_clock::now();
    lastTime = startTime;
    currentTime = {};
}

void TimeManager::Update()
{
    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);

    uint64_t deltaMs = delta.count();

    // Breakpoint resume case
    if (deltaMs > 1000) { deltaMs = 333; }

    currentTime.deltaTime = static_cast<float>(deltaMs) / 1000.0f;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
    currentTime.totalTime = static_cast<float>(elapsed.count()) / 1000.0f;

    currentTime.frameCount++;

    lastTime = now;
}
} // Core
