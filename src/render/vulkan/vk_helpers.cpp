//
// Created by William on 2025-12-11.
//

#include "vk_helpers.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace Render
{
VkImageMemoryBarrier2 VkHelpers::ImageMemoryBarrier(
    VkImage image,
    const VkImageSubresourceRange& subresourceRange,
    const VkPipelineStageFlagBits2 srcStageMask,
    const VkAccessFlagBits2 srcAccessMask,
    const VkImageLayout oldLayout,
    const VkPipelineStageFlagBits2 dstStageMask,
    const VkAccessFlagBits2 dstAccessMask,
    const VkImageLayout newLayout
)
{
    return {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .image = image,
        .subresourceRange = subresourceRange,
    };
}

VkBufferMemoryBarrier2 VkHelpers::BufferMemoryBarrier(
    VkBuffer buffer,
    const VkDeviceSize offset,
    const VkDeviceSize size,
    const VkPipelineStageFlagBits2 srcStageMask,
    const VkAccessFlagBits2 srcAccessMask,
    const VkPipelineStageFlagBits2 dstStageMask,
    const VkAccessFlagBits2 dstAccessMask
)
{
    return {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .offset = offset,
        .size = size,
    };
}

VkImageSubresourceRange VkHelpers::SubresourceRange(const VkImageAspectFlags aspectMask, const uint32_t levelCount, const uint32_t layerCount)
{
    return {
        .aspectMask = aspectMask,
        .baseMipLevel = 0,
        .levelCount = levelCount,
        .baseArrayLayer = 0,
        .layerCount = layerCount,
    };
}

VkImageSubresourceRange VkHelpers::SubresourceRange(const VkImageAspectFlags aspectMask, const uint32_t baseMipLevel, const uint32_t levelCount, const uint32_t baseArrayLayer,
                                                    const uint32_t layerCount)
{
    return {
        .aspectMask = aspectMask,
        .baseMipLevel = baseMipLevel,
        .levelCount = levelCount,
        .baseArrayLayer = baseArrayLayer,
        .layerCount = layerCount,
    };
}

VkDependencyInfo VkHelpers::DependencyInfo(VkImageMemoryBarrier2* imageBarrier)
{
    return {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .imageMemoryBarrierCount = imageBarrier ? 1u : 0u,
        .pImageMemoryBarriers = imageBarrier,
    };
}

VkDependencyInfo VkHelpers::DependencyInfo(VkImageMemoryBarrier2* imageBarrier, uint32_t count)
{
    return {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .imageMemoryBarrierCount = count,
        .pImageMemoryBarriers = imageBarrier,
    };
}

VkCommandPoolCreateInfo VkHelpers::CommandPoolCreateInfo(uint32_t queueFamilyIndex)
{
    return {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndex,
    };
}

VkCommandBufferAllocateInfo VkHelpers::CommandBufferAllocateInfo(uint32_t bufferCount, VkCommandPool commandPool)
{
    return {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = bufferCount,
    };
}

VkFenceCreateInfo VkHelpers::FenceCreateInfo()
{
    return {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
}

VkSemaphoreCreateInfo VkHelpers::SemaphoreCreateInfo()
{
    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
    };
}

VkCommandBufferBeginInfo VkHelpers::CommandBufferBeginInfo()
{
    return {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
}

VkCommandBufferSubmitInfo VkHelpers::CommandBufferSubmitInfo(VkCommandBuffer cmd)
{
    return {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext = nullptr,
        .commandBuffer = cmd,
        .deviceMask = 0,
    };
}

VkSubmitInfo2 VkHelpers::SubmitInfo(VkCommandBufferSubmitInfo* commandBufferSubmitInfo, const VkSemaphoreSubmitInfo* waitSemaphoreInfo, const VkSemaphoreSubmitInfo* signalSemaphoreInfo)
{
    return {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .pNext = nullptr,
        .waitSemaphoreInfoCount = waitSemaphoreInfo == nullptr ? 0u : 1u,
        .pWaitSemaphoreInfos = waitSemaphoreInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = commandBufferSubmitInfo,
        .signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0u : 1u,
        .pSignalSemaphoreInfos = signalSemaphoreInfo,
    };
}

VkSemaphoreSubmitInfo VkHelpers::SemaphoreSubmitInfo(VkSemaphore semaphore, VkPipelineStageFlags2 stageMask)
{
    return {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .semaphore = semaphore,
        .value = 1,
        .stageMask = stageMask,
        .deviceIndex = 0,
    };
}

VkPresentInfoKHR VkHelpers::PresentInfo(VkSwapchainKHR* swapchain, VkSemaphore* waitSemaphore, uint32_t* imageIndices)
{
    return {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = swapchain,
        .pImageIndices = imageIndices,
    };
}

VkDeviceSize VkHelpers::GetAlignedSize(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

VkDeviceAddress VkHelpers::GetDeviceAddress(VkDevice device, VkBuffer buffer)
{
    VkBufferDeviceAddressInfo bufferDeviceAddressInfo{};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = buffer;
    const uint64_t address = vkGetBufferDeviceAddress(device, &bufferDeviceAddressInfo);
    return address;
}

VkImageCreateInfo VkHelpers::ImageCreateInfo(VkFormat format, VkExtent3D extent, VkFlags usageFlags)
{
    return {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,

        // Single 2D image with no mip levels by default
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,

        // No MSAA
        .samples = VK_SAMPLE_COUNT_1_BIT,

        // Tiling Optimal has the best performance
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usageFlags,

        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
}

VkImageViewCreateInfo VkHelpers::ImageViewCreateInfo(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageSubresourceRange subresource{
        .aspectMask = aspectFlags,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    return {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = subresource,
    };
}

bool VkHelpers::LoadShaderModule(const std::filesystem::path& filePath, VkDevice device, VkShaderModule* outShaderModule)
{
    // open the file. With cursor at the end
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);


    if (!file.is_open()) {
        return false;
    }

    // find what the size of the file is by looking up the location of the cursor
    // because the cursor is at the end, it gives the size directly in bytes
    size_t fileSize = (size_t) file.tellg();

    // spirv expects the buffer to be on uint32, so make sure to reserve a int
    // vector big enough for the entire file
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    // put file cursor at beginning
    file.seekg(0);

    // load the entire file into the buffer
    file.read((char*) buffer.data(), fileSize);

    // now that the file is loaded into the buffer, we can close it
    file.close();

    // create a new shader module, using the buffer we loaded
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;

    // codeSize has to be in bytes, so multply the ints in the buffer by size of
    // int to know the real size of the buffer
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    // check that the creation goes well.
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return false;
    }
    *outShaderModule = shaderModule;
    return true;
}

VkPipelineShaderStageCreateInfo VkHelpers::PipelineShaderStageCreateInfo(VkShaderModule shader, VkShaderStageFlagBits shaderStage)
{
    return {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .stage = shaderStage,
        .module = shader,
        .pName = "main",
    };
}

VkComputePipelineCreateInfo VkHelpers::ComputePipelineCreateInfo(VkPipelineLayout pipelineLayout, const VkPipelineShaderStageCreateInfo& pipelineStageCreateInfo)
{
    return {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .stage = pipelineStageCreateInfo,
        .layout = pipelineLayout,
    };
}

VkRenderingAttachmentInfo VkHelpers::RenderingAttachmentInfo(VkImageView view, const VkClearValue* clear, VkImageLayout layout)
{
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = view,
        .imageLayout = layout,
        .loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear ? *clear : VkClearValue{}
    };
}

VkRenderingInfo VkHelpers::RenderingInfo(const VkExtent2D renderExtent, const VkRenderingAttachmentInfo* colorAttachment, const VkRenderingAttachmentInfo* depthAttachment)
{
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .renderArea = VkRect2D{VkOffset2D{0, 0}, renderExtent},
        .layerCount = 1,
        .colorAttachmentCount = colorAttachment == nullptr ? 0u : 1u,
        .pColorAttachments = colorAttachment,
        .pDepthAttachment = depthAttachment,
        .pStencilAttachment = nullptr,
    };
}

VkRenderingInfo VkHelpers::RenderingInfo(const VkExtent2D renderExtent, const VkRenderingAttachmentInfo* colorAttachment, uint32_t colorAttachmentCount, const VkRenderingAttachmentInfo* depthAttachment)
{
    return {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .renderArea = VkRect2D{VkOffset2D{0, 0}, renderExtent},
        .layerCount = 1,
        .colorAttachmentCount = colorAttachmentCount,
        .pColorAttachments = colorAttachment,
        .pDepthAttachment = depthAttachment,
        .pStencilAttachment = nullptr,
    };
}

VkViewport VkHelpers::GenerateFlippedViewport(uint32_t width, uint32_t height)
{
    return {
        .x = 0.0f,
        .y = static_cast<float>(height),
        .width = static_cast<float>(width),
        .height = -static_cast<float>(height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
}

VkViewport VkHelpers::GenerateViewport(uint32_t width, uint32_t height)
{
    return {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(width),
        .height = static_cast<float>(height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
}

VkRect2D VkHelpers::GenerateScissor(uint32_t width, uint32_t height)
{
    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = width;
    scissor.extent.height = height;
    return scissor;
}

uint32_t VkHelpers::GetBytesPerPixel(VkFormat format)
{
    {
        switch (format) {
            // 8-bit formats
            case VK_FORMAT_R8_UNORM:
            case VK_FORMAT_R8_SNORM:
            case VK_FORMAT_R8_UINT:
            case VK_FORMAT_R8_SINT:
            case VK_FORMAT_R8_SRGB:
                return 1;
            case VK_FORMAT_R8G8_UNORM:
            case VK_FORMAT_R8G8_SNORM:
            case VK_FORMAT_R8G8_UINT:
            case VK_FORMAT_R8G8_SINT:
            case VK_FORMAT_R8G8_SRGB:
                return 2;
            case VK_FORMAT_R8G8B8_UNORM:
            case VK_FORMAT_R8G8B8_SNORM:
            case VK_FORMAT_R8G8B8_UINT:
            case VK_FORMAT_R8G8B8_SINT:
            case VK_FORMAT_R8G8B8_SRGB:
            case VK_FORMAT_B8G8R8_UNORM:
            case VK_FORMAT_B8G8R8_SNORM:
            case VK_FORMAT_B8G8R8_UINT:
            case VK_FORMAT_B8G8R8_SINT:
            case VK_FORMAT_B8G8R8_SRGB:
                return 3;
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SNORM:
            case VK_FORMAT_R8G8B8A8_UINT:
            case VK_FORMAT_R8G8B8A8_SINT:
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_SNORM:
            case VK_FORMAT_B8G8R8A8_UINT:
            case VK_FORMAT_B8G8R8A8_SINT:
            case VK_FORMAT_B8G8R8A8_SRGB:
                return 4;
            case VK_FORMAT_R16_UNORM:
            case VK_FORMAT_R16_SNORM:
            case VK_FORMAT_R16_UINT:
            case VK_FORMAT_R16_SINT:
            case VK_FORMAT_R16_SFLOAT:
                return 2;
            case VK_FORMAT_R16G16_UNORM:
            case VK_FORMAT_R16G16_SNORM:
            case VK_FORMAT_R16G16_UINT:
            case VK_FORMAT_R16G16_SINT:
            case VK_FORMAT_R16G16_SFLOAT:
                return 4;
            case VK_FORMAT_R16G16B16_UNORM:
            case VK_FORMAT_R16G16B16_SNORM:
            case VK_FORMAT_R16G16B16_UINT:
            case VK_FORMAT_R16G16B16_SINT:
            case VK_FORMAT_R16G16B16_SFLOAT:
                return 6;
            case VK_FORMAT_R16G16B16A16_UNORM:
            case VK_FORMAT_R16G16B16A16_SNORM:
            case VK_FORMAT_R16G16B16A16_UINT:
            case VK_FORMAT_R16G16B16A16_SINT:
            case VK_FORMAT_R16G16B16A16_SFLOAT:
                return 8;
            case VK_FORMAT_R32_UINT:
            case VK_FORMAT_R32_SINT:
            case VK_FORMAT_R32_SFLOAT:
                return 4;
            case VK_FORMAT_R32G32_UINT:
            case VK_FORMAT_R32G32_SINT:
            case VK_FORMAT_R32G32_SFLOAT:
                return 8;
            case VK_FORMAT_R32G32B32_UINT:
            case VK_FORMAT_R32G32B32_SINT:
            case VK_FORMAT_R32G32B32_SFLOAT:
                return 12;
            case VK_FORMAT_R32G32B32A32_UINT:
            case VK_FORMAT_R32G32B32A32_SINT:
            case VK_FORMAT_R32G32B32A32_SFLOAT:
                return 16;
            // Compressed formats (return 0, require special handling)
            case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
            case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
            case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            case VK_FORMAT_BC4_UNORM_BLOCK:
            case VK_FORMAT_BC4_SNORM_BLOCK:
            //return 0; // 0.5 bytes per pixel (4x4 blocks, 8 bytes per block)
            case VK_FORMAT_BC2_UNORM_BLOCK:
            case VK_FORMAT_BC2_SRGB_BLOCK:
            case VK_FORMAT_BC3_UNORM_BLOCK:
            case VK_FORMAT_BC3_SRGB_BLOCK:
            case VK_FORMAT_BC5_UNORM_BLOCK:
            case VK_FORMAT_BC5_SNORM_BLOCK:
            case VK_FORMAT_BC6H_UFLOAT_BLOCK:
            case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            case VK_FORMAT_BC7_UNORM_BLOCK:
            case VK_FORMAT_BC7_SRGB_BLOCK:
                //return 0; // 1 byte per pixel (4x4 blocks, 16 bytes per block)
            default:
                assert(false && "Unsupported format");
                return 0;
        }
    }
}
} // Render
