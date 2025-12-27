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

struct PipelineEvent
{
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
};

struct ResourceDimensions
{
    enum class Type { Image, Buffer } type = Type::Image;

    // Image fields
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t levels = 1;
    uint32_t layers = 1;
    uint32_t samples = 1;
    VkImageUsageFlags imageUsage = 0;

    // Buffer fields
    VkDeviceSize bufferSize = 0;
    VkBufferUsageFlags bufferUsage = 0;

    // Shared
    std::string name;
    // RenderGraphQueueFlags queues = 0;
    uint32_t flags = 0; // PERSISTENT_BIT, TRANSIENT_BIT, etc.

    bool is_buffer() const { return type == Type::Buffer; }
    bool is_image() const { return type == Type::Image; }

    // For aliasing - check if two resources can share physical memory
    bool operator==(const ResourceDimensions& other) const
    {
        if (type != other.type) return false;

        if (is_buffer()) {
            return bufferSize == other.bufferSize &&
                   bufferUsage == other.bufferUsage;
        }
        else {
            return format == other.format &&
                   width == other.width &&
                   height == other.height &&
                   depth == other.depth &&
                   levels == other.levels &&
                   layers == other.layers &&
                   samples == other.samples &&
                   imageUsage == other.imageUsage;
        }
    }
};

struct PhysicalResource
{
    ResourceDimensions dimensions;
    PipelineEvent event;
    bool bIsImported = false;

    // Image resources (valid if dimensions.is_image())
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation imageAllocation = VK_NULL_HANDLE;
    uint32_t descriptorIndex = UINT32_MAX;
    bool descriptorWritten = false;

    // Buffer resources (valid if dimensions.is_buffer())
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation bufferAllocation = VK_NULL_HANDLE;
    VkDeviceAddress bufferAddress = 0;
    bool addressRetrieved = false;

    bool IsAllocated() const
    {
        return dimensions.is_image() ? (image != VK_NULL_HANDLE) : (buffer != VK_NULL_HANDLE);
    }

    bool NeedsDescriptorWrite() const
    {
        return dimensions.is_image() && IsAllocated() && !descriptorWritten;
    }

    bool NeedsAddressRetrieval() const
    {
        return dimensions.is_buffer() && IsAllocated() && !addressRetrieved;
    }
};

struct TextureResource
{
    std::string name;
    uint32_t index;
    uint32_t physicalIndex = UINT32_MAX;

    TextureInfo textureInfo;
    TextureUsageType usageType = TextureUsageType::Unknown;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    [[nodiscard]] bool HasPhysical() const { return physicalIndex != UINT32_MAX; }

    [[nodiscard]] bool HasFinalLayout() const { return finalLayout != VK_IMAGE_LAYOUT_UNDEFINED; }
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_RESOURCES_H
