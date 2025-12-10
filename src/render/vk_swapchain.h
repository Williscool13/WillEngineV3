//
// Created by William on 2025-10-10.
//

#ifndef WILLENGINETESTBED_SWAPCHAIN_H
#define WILLENGINETESTBED_SWAPCHAIN_H

#include <vector>

#include <volk.h>

#include "vk_config.h"

namespace Render
{
struct VulkanContext;

struct Swapchain
{
    Swapchain() = delete;

    explicit Swapchain(const VulkanContext* context, uint32_t width, uint32_t height);

    ~Swapchain();

    void Create(uint32_t width, uint32_t height);

    void Recreate(uint32_t width, uint32_t height);

    void Dump();

    [[nodiscard]] bool IsHDR() const { return format == SWAPCHAIN_HDR_FORMAT && colorSpace == SWAPCHAIN_HDR_COLORSPACE; }

    VkSwapchainKHR handle{};
    VkFormat format{};
    VkColorSpaceKHR colorSpace{};
    VkExtent2D extent{};
    uint32_t imageCount{};
    std::vector<VkImage> swapchainImages{};
    std::vector<VkImageView> swapchainImageViews{};

private:
    const VulkanContext* context;
};
} // Renderer

#endif //WILLENGINETESTBED_SWAPCHAIN_H
