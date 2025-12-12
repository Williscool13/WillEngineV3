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

struct FrameSynchronization
{
    VulkanContext* context{};

    VkCommandPool commandPool{};
    VkCommandBuffer commandBuffer{};
    VkFence renderFence{};
    VkSemaphore swapchainSemaphore{};
    VkSemaphore renderSemaphore{};

    FrameSynchronization() = default;
    explicit FrameSynchronization(VulkanContext* context);
    ~FrameSynchronization();

    FrameSynchronization(const FrameSynchronization&) = delete;
    FrameSynchronization& operator=(const FrameSynchronization&) = delete;

    FrameSynchronization(FrameSynchronization&& other) noexcept;
    FrameSynchronization& operator=(FrameSynchronization&& other) noexcept;

    void Initialize();
    void RecreateSynchronization();
};
} // Renderer

#endif //WILLENGINETESTBED_SYNCHRONIZATION_H
