//
// Created by William on 2025-12-10.
//

#ifndef WILL_ENGINE_VK_CONFIG_H
#define WILL_ENGINE_VK_CONFIG_H

#include <volk.h>

namespace Render
{
inline constexpr bool ENABLE_HDR = false;

// Swapchain - HDR (HDR10)
inline constexpr VkFormat SWAPCHAIN_HDR_FORMAT = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
inline constexpr VkColorSpaceKHR SWAPCHAIN_HDR_COLORSPACE = VK_COLOR_SPACE_HDR10_ST2084_EXT;

// Swapchain - SDR
inline constexpr VkFormat SWAPCHAIN_SDR_FORMAT = VK_FORMAT_B8G8R8A8_SRGB;
inline constexpr VkColorSpaceKHR SWAPCHAIN_SDR_COLORSPACE = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

// Swapchain - Present
inline constexpr VkPresentModeKHR SWAPCHAIN_PRESENT_MODE = VK_PRESENT_MODE_FIFO_KHR;

// Render targets
inline constexpr VkFormat COLOR_ATTACHMENT_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr VkFormat DEPTH_ATTACHMENT_FORMAT = VK_FORMAT_D32_SFLOAT;
inline constexpr VkFormat GBUFFER_ALBEDO_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
inline constexpr VkFormat GBUFFER_NORMAL_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT; // UNORM 101010 is probably fine
inline constexpr VkFormat GBUFFER_PBR_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
inline constexpr VkFormat GBUFFER_MOTION_FORMAT = VK_FORMAT_R16G16_SFLOAT;
} // Render

#endif //WILL_ENGINE_VK_CONFIG_H