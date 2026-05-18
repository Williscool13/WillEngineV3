//
// Created by William on 2026-04-11.
//

#include "frame_sync.h"

namespace Core
{
FrameSync::FrameSync(MemoryManager& memoryManager)
{
    constexpr Core::AllocTag kFrameTags[] = {
        Core::AllocTag::FrameSync0, Core::AllocTag::FrameSync1,
        Core::AllocTag::FrameSync2, Core::AllocTag::FrameSync3,
    };
    for (size_t i = 0; i < frameBuffers.Size(); ++i) {
        frameBuffers[i] = FrameBuffer(memoryManager.ArenaPool(), kFrameTags[i]);
    }

    stagingFrameBuffer = FrameBuffer(memoryManager.ArenaPool(), Core::AllocTag::FrameSync3);
}
} // Core
