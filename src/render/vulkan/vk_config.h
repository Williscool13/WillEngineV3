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
inline constexpr VkFormat DEPTH_ATTACHMENT_FORMAT = VK_FORMAT_D32_SFLOAT_S8_UINT;

inline constexpr VkFormat VISIBILITY_BUFFER_FORMAT = VK_FORMAT_R32G32_UINT;
inline constexpr VkFormat GBUFFER_STABLE_ID_FORMAT = VK_FORMAT_R32G32_UINT;

// Half is imprecise. Anything larger than a few units will show.
inline constexpr VkFormat VISIBILITY_BARYCENTRIC_FORMAT = VK_FORMAT_R32G32_SFLOAT;
// Keep an eye for mip selection artifacts
inline constexpr VkFormat VISIBILITY_DERIVATIVES_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

// R: Normal oct16 RG16 packed into R32
// G: Motion vectors XY R16G16 | 16-bit spare
// B: Roughness 15-bit | Metalness 1-bit
// A: bit 0 = geometry marker (shading passes write 1; cleared 0 = sky/no geometry) | bits 16-31 = world-space Z delta float16
inline constexpr VkFormat GBUFFER_TARGET_ONE = VK_FORMAT_R32G32B32A32_UINT;

// R: Albedo RGB8 | 8-bit spare
// G: Emissive RGBE 9:9:9:5 packed into R32
inline constexpr VkFormat GBUFFER_TARGET_TWO = VK_FORMAT_R32G32_UINT;

// Shadows
inline constexpr VkFormat SHADOW_CASCADE_FORMAT = VK_FORMAT_D32_SFLOAT;
} // Render

#endif //WILL_ENGINE_VK_CONFIG_H