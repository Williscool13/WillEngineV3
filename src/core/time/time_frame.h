//
// Created by William on 2025-12-14.
//

#ifndef WILL_ENGINE_TIME_FRAME_H
#define WILL_ENGINE_TIME_FRAME_H

#include <cstdint>

namespace Core
{
struct TimeFrame
{
    float deltaTime;
    float totalTime;
    uint64_t frameCount;
    float gameFps;

    float renderDeltaTime;
    float renderTotalTime;
    float renderFps;

    // Written by Render Thread
    float renderWallMs;
    float gpuFrameMs;
};
} // Core

using TimeFrame = Core::TimeFrame;

#endif //WILL_ENGINE_TIME_FRAME_H
