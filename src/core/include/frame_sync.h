//
// Created by William on 2025-12-12.
//

#ifndef WILL_ENGINE_FRAME_SYNC_H
#define WILL_ENGINE_FRAME_SYNC_H
#include <array>
#include <semaphore>

#include "render_interface.h"

namespace Core
{
struct FrameSync
{
    std::array<FrameBuffer, FRAME_BUFFER_COUNT> frameBuffers{};
    std::counting_semaphore<FRAME_BUFFER_COUNT> gameFrames{FRAME_BUFFER_COUNT};
    std::counting_semaphore<FRAME_BUFFER_COUNT> renderFrames{0};
};
} // Core

#endif //WILL_ENGINE_FRAME_SYNC_H