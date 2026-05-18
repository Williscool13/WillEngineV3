//
// Created by William on 2026-04-11.
//

#include "frame_sync.h"

namespace Core
{
FrameSync::FrameSync(MemoryManager& memoryManager)
{
    for (auto& frameBuffer : frameBuffers) {
        frameBuffer = FrameBuffer(memoryManager.ArenaPool());
    }

    stagingFrameBuffer = FrameBuffer(memoryManager.ArenaPool());
}
} // Core
