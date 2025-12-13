//
// Created by William on 2025-12-11.
//

#ifndef WILL_ENGINE_VK_HELPERS_H
#define WILL_ENGINE_VK_HELPERS_H

#include <filesystem>
#include <volk.h>

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

VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(VkShaderModule computeShader, VkShaderStageFlagBits shaderStage);

VkComputePipelineCreateInfo ComputePipelineCreateInfo(VkPipelineLayout pipelineLayout, const VkPipelineShaderStageCreateInfo& pipelineStageCreateInfo);

VkRenderingAttachmentInfo RenderingAttachmentInfo(VkImageView view, const VkClearValue* clear, VkImageLayout layout);

VkRenderingInfo RenderingInfo(VkExtent2D renderExtent, const VkRenderingAttachmentInfo* colorAttachment, const VkRenderingAttachmentInfo* depthAttachment);

VkViewport GenerateViewport(uint32_t width, uint32_t height);

VkRect2D GenerateScissor(uint32_t width, uint32_t height);
} // Render

#endif //WILL_ENGINE_VK_HELPERS_H
