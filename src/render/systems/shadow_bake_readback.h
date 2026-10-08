//
// Created by William on 2026-10-09.
//

#ifndef WILL_ENGINE_SHADOW_BAKE_READBACK_H
#define WILL_ENGINE_SHADOW_BAKE_READBACK_H

#include <atomic>
#include <cstdint>

#include "render/vulkan/vk_resources.h"

namespace Render
{
struct VulkanContext;

/** Host-mapped landing buffer for one baked shadow-map face. */
struct ShadowBakeReadback
{
    VulkanContext* context{};
    AllocatedBuffer buffer{};
    uint32_t resolution{0};
    uint64_t requestId{0};
    uint64_t lastAcceptedRequestId{0};
    uint32_t pendingSlot{UINT32_MAX};
    std::atomic<bool> bReady{false};

    [[nodiscard]] bool CanAccept(uint64_t request) const { return pendingSlot == UINT32_MAX && !bReady.load(std::memory_order_acquire) && request != lastAcceptedRequestId; }

    VkDeviceAddress Begin(uint64_t request, uint32_t faceResolution, uint32_t frameSlot);

    void Resolve(uint32_t frameSlot);

    [[nodiscard]] const uint16_t* Pixels() const { return static_cast<const uint16_t*>(buffer.allocationInfo.pMappedData); }

    void Release() { bReady.store(false, std::memory_order_release); }
};
} // Render

#endif //WILL_ENGINE_SHADOW_BAKE_READBACK_H
