//
// Created by William on 2026-10-09.
//

#include "shadow_bake_readback.h"

#include "render/vulkan/vk_context.h"

namespace Render
{
VkDeviceAddress ShadowBakeReadback::Begin(uint64_t request, uint32_t faceResolution, uint32_t frameSlot)
{
    if (resolution != faceResolution || buffer.handle == VK_NULL_HANDLE) {
        buffer = AllocatedBuffer::CreateAllocatedReceivingBuffer(context, static_cast<size_t>(faceResolution) * faceResolution * sizeof(uint16_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        buffer.SetDebugName("shadow_bake_readback");
        resolution = faceResolution;
    }
    requestId = request;
    lastAcceptedRequestId = request;
    pendingSlot = frameSlot;
    return buffer.address;
}

void ShadowBakeReadback::Resolve(uint32_t frameSlot)
{
    if (pendingSlot == frameSlot) {
        pendingSlot = UINT32_MAX;
        bReady.store(true, std::memory_order_release);
    }
}
} // Render
