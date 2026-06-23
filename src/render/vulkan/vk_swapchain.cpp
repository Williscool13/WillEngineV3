//
// Created by William on 2025-12-11.
//

#include "vk_swapchain.h"

#include <algorithm>

#include "vk_config.h"
#include "core/containers/array.h"
#include "vk_context.h"
#include "vk_utils.h"
#include "engine/logging/engine_log.h"
#include "render/interface/render_interface.h"

namespace Render
{
Swapchain::Swapchain(const VulkanContext* context, uint32_t width, uint32_t height) : context(context)
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
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physicalDevice, context->surface, &caps);

    // Select format
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physicalDevice, context->surface, &formatCount, nullptr);
    assert(formatCount > 0 && formatCount <= 64);
    Core::Array<VkSurfaceFormatKHR, 64> formats;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physicalDevice, context->surface, &formatCount, formats.Data());

    const VkSurfaceFormatKHR desired = ENABLE_HDR
                                           ? VkSurfaceFormatKHR{SWAPCHAIN_HDR_FORMAT, SWAPCHAIN_HDR_COLORSPACE}
                                           : VkSurfaceFormatKHR{SWAPCHAIN_SDR_FORMAT, SWAPCHAIN_SDR_COLORSPACE};
    const VkSurfaceFormatKHR fallback{SWAPCHAIN_SDR_FORMAT, SWAPCHAIN_SDR_COLORSPACE};

    VkSurfaceFormatKHR selectedFormat = formats[0];
    for (uint32_t i = 0; i < formatCount; i++) {
        if (formats[i].format == desired.format && formats[i].colorSpace == desired.colorSpace) {
            selectedFormat = formats[i];
            break;
        }
        if (formats[i].format == fallback.format && formats[i].colorSpace == fallback.colorSpace) {
            selectedFormat = formats[i];
        }
    }

    // Select present mode (FIFO is always guaranteed)
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physicalDevice, context->surface, &presentModeCount, nullptr);
    assert(presentModeCount <= 8);
    Core::Array<VkPresentModeKHR, 8> presentModes;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physicalDevice, context->surface, &presentModeCount, presentModes.Data());

    VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < presentModeCount; i++) {
        if (presentModes[i] == SWAPCHAIN_PRESENT_MODE) {
            selectedPresentMode = SWAPCHAIN_PRESENT_MODE;
            break;
        }
    }

    // Determine extent
    VkExtent2D selectedExtent;
    if (caps.currentExtent.width != UINT32_MAX) {
        selectedExtent = caps.currentExtent;
    }
    else {
        selectedExtent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        selectedExtent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    // Triple buffer, respecting surface limits
    uint32_t selectedImageCount = std::max(caps.minImageCount, Core::FRAME_BUFFER_COUNT);
    if (caps.maxImageCount > 0) {
        selectedImageCount = std::min(selectedImageCount, caps.maxImageCount);
    }

    assert(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    assert(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = context->surface;
    createInfo.minImageCount = selectedImageCount;
    createInfo.imageFormat = selectedFormat.format;
    createInfo.imageColorSpace = selectedFormat.colorSpace;
    createInfo.imageExtent = selectedExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = selectedPresentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(context->device, &createInfo, nullptr, &handle);
    if (result != VK_SUCCESS) {
        LOG_ERROR(Renderer, "Failed to create swapchain: {}", string_VkResult(result));
        std::abort();
    }

    uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(context->device, handle, &actualImageCount, nullptr);
    assert(actualImageCount >= Core::FRAME_BUFFER_COUNT);
    Core::Array<VkImage, Core::FRAME_BUFFER_COUNT + 2> images;
    vkGetSwapchainImagesKHR(context->device, handle, &actualImageCount, images.Data());

    format = selectedFormat.format;
    colorSpace = selectedFormat.colorSpace;
    extent = selectedExtent;
    usages = createInfo.imageUsage;
    imageCount = Core::FRAME_BUFFER_COUNT;

    for (uint32_t i = 0; i < Core::FRAME_BUFFER_COUNT; i++) {
        swapchainImages.PushBack(images[i]);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = selectedFormat.format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView view = VK_NULL_HANDLE;
        vkCreateImageView(context->device, &viewInfo, nullptr, &view);
        swapchainImageViews.PushBack(view);
    }
}

void Swapchain::Recreate(uint32_t width, uint32_t height)
{
    vkQueueWaitIdle(context->graphicsQueue);
    vkDestroySwapchainKHR(context->device, handle, nullptr);
    for (const auto swapchainImage : swapchainImageViews) {
        vkDestroyImageView(context->device, swapchainImage, nullptr);
    }

    swapchainImages.Clear();
    swapchainImageViews.Clear();
    Create(width, height);
    Dump();
}

void Swapchain::Dump()
{
    LOG_INFO(Renderer, "=== Swapchain Info ===");
    LOG_INFO(Renderer, "Image Count: {}", imageCount);
    LOG_INFO(Renderer, "Format: {}", string_VkFormat(format));
    LOG_INFO(Renderer, "Color Space: {}", string_VkColorSpaceKHR(colorSpace));
    LOG_INFO(Renderer, "Extent: {}x{}", extent.width, extent.height);
    LOG_INFO(Renderer, "Images: {}", swapchainImages.Size());
    LOG_INFO(Renderer, "Image Views: {}", swapchainImageViews.Size());
}
} // Renderer
