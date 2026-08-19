//
// Created by William on 2026-04-11.
//

#include "frame_sync.h"

namespace Core
{
FrameSync::FrameSync(MemoryManager& memoryManager)
{
    constexpr AllocTag kFrameTags[] = {
        AllocTag::FrameSync0, AllocTag::FrameSync1,
        AllocTag::FrameSync2, AllocTag::FrameSync3,
    };
    constexpr const char* kFrameNames[] = {"frame0", "frame1", "frame2", "frame3"};
    for (size_t i = 0; i < frameBuffers.Size(); ++i) {
        frameBuffers[i].Initialize(memoryManager.ArenaPool(), kFrameTags[i], kFrameNames[i]);
    }
}
} // Core
