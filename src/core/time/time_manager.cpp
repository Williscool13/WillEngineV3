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
    lastRenderTime = startTime;
}

void TimeManager::Reset()
{
    startTime = std::chrono::steady_clock::now();
    lastTime = startTime;
    lastRenderTime = startTime;
    currentTime = {};
}

void TimeManager::UpdateGame()
{
    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);

    uint64_t deltaMs = delta.count();
    if (deltaMs > 1000) { deltaMs = 333; }

    currentTime.deltaTime = static_cast<float>(deltaMs) / 1000.0f;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
    currentTime.totalTime = static_cast<float>(elapsed.count()) / 1000.0f;

    currentTime.frameCount++;
    lastTime = now;
}

void TimeManager::UpdateRender()
{
    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::microseconds>(now - lastRenderTime);

    uint64_t deltaUs = delta.count();
    if (deltaUs > 1000000) { deltaUs = 333000; }

    currentTime.renderDeltaTime = static_cast<float>(deltaUs) / 1000000.0f;

    const float dt = currentTime.renderDeltaTime;
    if (dt > 0.0f) {
        smoothedRenderDeltaTime = (smoothedRenderDeltaTime <= 0.0f) ? dt : smoothedRenderDeltaTime + (dt - smoothedRenderDeltaTime) * 0.02f;
    }
    currentTime.renderFps = smoothedRenderDeltaTime > 0.0f ? 1.0f / smoothedRenderDeltaTime : 0.0f;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
    currentTime.renderTotalTime = static_cast<float>(elapsed.count()) / 1000.0f;

    lastRenderTime = now;
}
} // Core
