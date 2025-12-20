//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_HELPERS_H
#define WILL_ENGINE_VK_HELPERS_H

#include <filesystem>
#include <volk.h>

#include "core/include/render_interface.h"

namespace Render::VkHelpers
{
VkImageMemoryBarrier2 ImageMemoryBarrier(VkImage image, const VkImageSubresourceRange& subresourceRange,
                                         VkPipelineStageFlagBits2 srcStageMask, VkAccessFlagBits2 srcAccessMask, VkImageLayout oldLayout,
                                         VkPipelineStageFlagBits2 dstStageMask, VkAccessFlagBits2 dstAccessMask, VkImageLayout newLayout);

VkBufferMemoryBarrier2 BufferMemoryBarrier(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
                                           VkPipelineStageFlagBits2 srcStageMask, VkAccessFlagBits2 srcAccessMask,
                                           VkPipelineStageFlagBits2 dstStageMask, VkAccessFlagBits2 dstAccessMask);

VkImageSubresourceRange SubresourceRange(VkImageAspectFlags aspectMask, uint32_t levelCount = VK_REMAINING_MIP_LEVELS, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS);

VkImageSubresourceRange SubresourceRange(VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount);

VkDependencyInfo DependencyInfo(VkImageMemoryBarrier2* imageBarrier);

VkCommandPoolCreateInfo CommandPoolCreateInfo(uint32_t queueFamilyIndex);

VkCommandBufferAllocateInfo CommandBufferAllocateInfo(uint32_t bufferCount, VkCommandPool commandPool = VK_NULL_HANDLE);

VkFenceCreateInfo FenceCreateInfo();

VkSemaphoreCreateInfo SemaphoreCreateInfo();

VkCommandBufferBeginInfo CommandBufferBeginInfo();

VkCommandBufferSubmitInfo CommandBufferSubmitInfo(VkCommandBuffer cmd);

VkSubmitInfo2 SubmitInfo(VkCommandBufferSubmitInfo* commandBufferSubmitInfo, const VkSemaphoreSubmitInfo* waitSemaphoreInfo, const VkSemaphoreSubmitInfo* signalSemaphoreInfo);

VkSemaphoreSubmitInfo SemaphoreSubmitInfo(VkSemaphore semaphore, VkPipelineStageFlags2 stageMask);

VkPresentInfoKHR PresentInfo(VkSwapchainKHR* swapchain, VkSemaphore* waitSemaphore, uint32_t* imageIndices);

VkDeviceSize GetAlignedSize(VkDeviceSize value, VkDeviceSize alignment);

VkDeviceAddress GetDeviceAddress(VkDevice device, VkBuffer buffer);

VkImageCreateInfo ImageCreateInfo(VkFormat format, VkExtent3D extent, VkFlags usageFlags);

VkImageViewCreateInfo ImageViewCreateInfo(VkImage image, VkFormat format, VkFlags aspectFlags);

bool LoadShaderModule(const std::filesystem::path& filePath, VkDevice device, VkShaderModule* outShaderModule);

VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(VkShaderModule shader, VkShaderStageFlagBits shaderStage);

VkComputePipelineCreateInfo ComputePipelineCreateInfo(VkPipelineLayout pipelineLayout, const VkPipelineShaderStageCreateInfo& pipelineStageCreateInfo);

VkRenderingAttachmentInfo RenderingAttachmentInfo(VkImageView view, const VkClearValue* clear, VkImageLayout layout);

VkRenderingInfo RenderingInfo(VkExtent2D renderExtent, const VkRenderingAttachmentInfo* colorAttachment, const VkRenderingAttachmentInfo* depthAttachment);

VkViewport GenerateViewport(uint32_t width, uint32_t height);

VkRect2D GenerateScissor(uint32_t width, uint32_t height);

inline VkBufferMemoryBarrier2 ToVkBarrier(const Core::BufferAcquireOperation& op) {
    return VkBufferMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = op.srcStageMask,
        .srcAccessMask = op.srcAccessMask,
        .dstStageMask = op.dstStageMask,
        .dstAccessMask = op.dstAccessMask,
        .srcQueueFamilyIndex = op.srcQueueFamilyIndex,
        .dstQueueFamilyIndex = op.dstQueueFamilyIndex,
        .buffer = reinterpret_cast<VkBuffer>(op.buffer),
        .offset = op.offset,
        .size = op.size
    };
}

inline VkImageMemoryBarrier2 ToVkBarrier(const Core::ImageAcquireOperation& op) {
    return VkImageMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = op.srcStageMask,
        .srcAccessMask = op.srcAccessMask,
        .dstStageMask = op.dstStageMask,
        .dstAccessMask = op.dstAccessMask,
        .oldLayout = static_cast<VkImageLayout>(op.oldLayout),
        .newLayout = static_cast<VkImageLayout>(op.newLayout),
        .srcQueueFamilyIndex = op.srcQueueFamilyIndex,
        .dstQueueFamilyIndex = op.dstQueueFamilyIndex,
        .image = reinterpret_cast<VkImage>(op.image),
        .subresourceRange = {
            .aspectMask = op.aspectMask,
            .baseMipLevel = op.baseMipLevel,
            .levelCount = op.levelCount,
            .baseArrayLayer = op.baseArrayLayer,
            .layerCount = op.layerCount
        }
    };
}

inline Core::BufferAcquireOperation FromVkBarrier(const VkBufferMemoryBarrier2& barrier) {
    return Core::BufferAcquireOperation{
        .buffer = reinterpret_cast<uint64_t>(barrier.buffer),
        .srcStageMask = barrier.srcStageMask,
        .srcAccessMask = barrier.srcAccessMask,
        .dstStageMask = barrier.dstStageMask,
        .dstAccessMask = barrier.dstAccessMask,
        .offset = barrier.offset,
        .size = barrier.size,
        .srcQueueFamilyIndex = barrier.srcQueueFamilyIndex,
        .dstQueueFamilyIndex = barrier.dstQueueFamilyIndex
    };
}

inline Core::ImageAcquireOperation FromVkBarrier(const VkImageMemoryBarrier2& barrier) {
    return Core::ImageAcquireOperation{
        .image = reinterpret_cast<uint64_t>(barrier.image),
        .aspectMask = barrier.subresourceRange.aspectMask,
        .baseMipLevel = barrier.subresourceRange.baseMipLevel,
        .levelCount = barrier.subresourceRange.levelCount,
        .baseArrayLayer = barrier.subresourceRange.baseArrayLayer,
        .layerCount = barrier.subresourceRange.layerCount,
        .srcStageMask = barrier.srcStageMask,
        .srcAccessMask = barrier.srcAccessMask,
        .oldLayout = static_cast<uint32_t>(barrier.oldLayout),
        .dstStageMask = barrier.dstStageMask,
        .dstAccessMask = barrier.dstAccessMask,
        .newLayout = static_cast<uint32_t>(barrier.newLayout),
        .srcQueueFamilyIndex = barrier.srcQueueFamilyIndex,
        .dstQueueFamilyIndex = barrier.dstQueueFamilyIndex
    };
}

uint32_t GetBytesPerPixel(VkFormat format);
} // Render

#endif //WILL_ENGINE_VK_HELPERS_H
