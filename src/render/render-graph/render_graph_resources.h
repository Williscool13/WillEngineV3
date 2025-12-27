//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_GRAPH_RESOURCES_H
#define WILL_ENGINE_RENDER_GRAPH_RESOURCES_H

#include <string>

#include <volk.h>

#include "render/vulkan/vk_resources.h"

namespace Render
{
enum class TextureUsageType
{
    Unknown,
    Storage,
    Sampled
};

struct TextureInfo
{
    VkFormat format;
    uint32_t width;
    uint32_t height;
    VkImageUsageFlags usage;

    TextureInfo() = default;

    TextureInfo(VkFormat fmt, uint32_t w, uint32_t h, VkImageUsageFlags u = 0)
        : format(fmt), width(w), height(h), usage(u) {}
};

struct TextureResource
{
    std::string name;
    uint32_t index;

    TextureInfo textureInfo;
    TextureUsageType usageType = TextureUsageType::Unknown;

    // Physical resource (set during Compile)
    // todo likely will not use these data types
    AllocatedImage image;
    ImageView view;
    uint32_t descriptorIndex = UINT32_MAX;

    // Runtime state (updated during Execute)
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Helpers
    [[nodiscard]] bool IsAllocated() const { return image.handle != VK_NULL_HANDLE; }
    [[nodiscard]] bool HasDescriptor() const { return descriptorIndex != UINT32_MAX; }
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_RESOURCES_H
