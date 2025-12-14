//
// Created by William on 2025-10-10.
//

#ifndef WILLENGINETESTBED_SYNCHRONIZATION_H
#define WILLENGINETESTBED_SYNCHRONIZATION_H

#include <chrono>
#include <volk/volk.h>

namespace Render
{
struct VulkanContext;

struct RenderSynchronization
{
    VulkanContext* context{};

    VkCommandPool commandPool{};
    VkCommandBuffer commandBuffer{};
    VkFence renderFence{};
    VkSemaphore swapchainSemaphore{};
    VkSemaphore renderSemaphore{};

    RenderSynchronization() = default;
    explicit RenderSynchronization(VulkanContext* context);
    ~RenderSynchronization();

    RenderSynchronization(const RenderSynchronization&) = delete;
    RenderSynchronization& operator=(const RenderSynchronization&) = delete;

    RenderSynchronization(RenderSynchronization&& other) noexcept;
    RenderSynchronization& operator=(RenderSynchronization&& other) noexcept;

    void Initialize();
    void RecreateSynchronization();
};
} // Renderer

#endif //WILLENGINETESTBED_SYNCHRONIZATION_H
