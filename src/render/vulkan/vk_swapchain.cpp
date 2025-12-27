//
// Created by William on 2025-12-11.
//

#include "vk_swapchain.h"

#include "VkBootstrap.h"
#include "vk_config.h"

#include "vk_context.h"
#include "vk_utils.h"

namespace Render
{
Swapchain::Swapchain(const VulkanContext* context, uint32_t width, uint32_t height): context(context)
{
    Create(width, height);
    Dump();
}

Swapchain::~Swapchain()
{
    vkDestroySwapchainKHR(context->device, handle, nullptr);
    for (VkImageView swapchainImageView : swapchainImageViews) {
        vkDestroyImageView(context->device, swapchainImageView, nullptr);
    }
}

void Swapchain::Create(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{context->physicalDevice, context->device, context->surface};

    if (ENABLE_HDR) {
        swapchainBuilder
            .set_desired_format({SWAPCHAIN_HDR_FORMAT, SWAPCHAIN_HDR_COLORSPACE})
            .add_fallback_format({SWAPCHAIN_SDR_FORMAT, SWAPCHAIN_SDR_COLORSPACE});
    } else {
        swapchainBuilder.set_desired_format({SWAPCHAIN_SDR_FORMAT, SWAPCHAIN_SDR_COLORSPACE});
    }

    auto swapchainResult = swapchainBuilder
            .set_desired_present_mode(SWAPCHAIN_PRESENT_MODE)
            .set_desired_extent(width, height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .set_required_min_image_count(vkb::SwapchainBuilder::TRIPLE_BUFFERING)
            .build();

    if (!swapchainResult) {
        SPDLOG_ERROR("Failed to create swapchain: {}", swapchainResult.error().message());
        SPDLOG_ERROR("Your GPU may not support the required features");
        std::abort();
    }

    vkb::Swapchain vkbSwapchain = swapchainResult.value();

    auto imagesResult = vkbSwapchain.get_images();
    auto viewsResult = vkbSwapchain.get_image_views();

    if (!imagesResult || !viewsResult) {
        SPDLOG_ERROR("Failed to get swapchain images/views");
        SPDLOG_ERROR("Your GPU may not support the required features");
        std::abort();
    }

    handle = vkbSwapchain.swapchain;
    imageCount = vkbSwapchain.image_count;
    format = vkbSwapchain.image_format;
    colorSpace = vkbSwapchain.color_space;
    extent = {vkbSwapchain.extent.width, vkbSwapchain.extent.height};
    usages = vkbSwapchain.image_usage_flags;
    swapchainImages = imagesResult.value();
    swapchainImageViews = viewsResult.value();
}

void Swapchain::Recreate(uint32_t width, uint32_t height)
{
    vkDeviceWaitIdle(context->device);
    vkDestroySwapchainKHR(context->device, handle, nullptr);
    for (const auto swapchainImage : swapchainImageViews) {
        vkDestroyImageView(context->device, swapchainImage, nullptr);
    }

    Create(width, height);
    Dump();
}

void Swapchain::Dump()
{
    SPDLOG_INFO("=== Swapchain Info ===");
    SPDLOG_INFO("Image Count: {}", imageCount);
    SPDLOG_INFO("Format: {}", string_VkFormat(format));
    SPDLOG_INFO("Color Space: {}", string_VkColorSpaceKHR(colorSpace));
    SPDLOG_INFO("Extent: {}x{}", extent.width, extent.height);
    SPDLOG_INFO("Images: {}", swapchainImages.size());
    SPDLOG_INFO("Image Views: {}", swapchainImageViews.size());
}
} // Renderer
