//
// Created by William on 2025-12-27.
//

#ifndef WILL_ENGINE_RENDER_GRAPH_RESOURCES_H
#define WILL_ENGINE_RENDER_GRAPH_RESOURCES_H

#include <string>
#include <vector>

#include <volk.h>

#include "core/allocators/handle.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct TextureResource;
using TransientImageHandle = Core::Handle<TextureResource>;

struct PipelineEvent
{
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

    [[nodiscard]] bool IsBuffer() const { return type == Type::Buffer; }
    [[nodiscard]] bool IsImage() const { return type == Type::Image; }

    bool operator==(const ResourceDimensions& other) const
    {
        return bufferSize == other.bufferSize &&
               bufferUsage == other.bufferUsage &&
               format == other.format &&
               width == other.width &&
               height == other.height &&
               depth == other.depth &&
               levels == other.levels &&
               layers == other.layers &&
               samples == other.samples;
    }
};

struct PhysicalResource
{
    ResourceDimensions dimensions;
    PipelineEvent event;
    bool bIsImported = false;
    bool bDisableBarriers = false;

    std::vector<uint32_t> logicalResourceIndices;
    bool bUsedThisFrame = false;

    // Image resources (valid if dimensions.is_image())
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation imageAllocation = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_NONE;
    TransientImageHandle descriptorHandle = TransientImageHandle::INVALID;
    bool descriptorWritten = false;

    // Buffer resources (valid if dimensions.is_buffer())
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation bufferAllocation = VK_NULL_HANDLE;
    VkDeviceAddress bufferAddress = 0;
    bool addressRetrieved = false;

    [[nodiscard]] bool IsAllocated() const { return dimensions.IsImage() ? (image != VK_NULL_HANDLE) : (buffer != VK_NULL_HANDLE); }

    [[nodiscard]] bool NeedsDescriptorWrite() const { return dimensions.IsImage() && IsAllocated() && !descriptorWritten; }

    [[nodiscard]] bool NeedsAddressRetrieval() const { return dimensions.IsBuffer() && IsAllocated() && !addressRetrieved && (dimensions.bufferUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) == VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT; }
};

struct TextureInfo
{
    VkFormat format;
    uint32_t width;
    uint32_t height;
};

struct TextureResource
{
    std::string name;
    uint32_t index;
    uint32_t physicalIndex = UINT32_MAX;

    TextureInfo textureInfo;

    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageUsageFlags accumulatedUsage;
    uint32_t firstPass = UINT32_MAX;
    uint32_t lastPass = 0;

    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    [[nodiscard]] bool HasPhysical() const { return physicalIndex != UINT32_MAX; }

    [[nodiscard]] bool HasFinalLayout() const { return finalLayout != VK_IMAGE_LAYOUT_UNDEFINED; }
};

struct BufferInfo
{
    VkDeviceSize size;
    VkBufferUsageFlags usage;
};

struct BufferResource
{
    std::string name;
    uint32_t index = UINT32_MAX;
    BufferInfo bufferInfo = {};
    uint32_t physicalIndex = UINT32_MAX;

    VkBufferUsageFlags accumulatedUsage = 0;
    uint32_t firstPass = UINT32_MAX;
    uint32_t lastPass = 0;

    [[nodiscard]] bool HasPhysical() const { return physicalIndex != UINT32_MAX; }
};
} // Render

#endif //WILL_ENGINE_RENDER_GRAPH_RESOURCES_H
