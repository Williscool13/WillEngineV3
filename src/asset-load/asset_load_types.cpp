//
// Created by William on 2025-12-18.
//

#include "asset_load_types.h"

#include "render/model/model_serialization.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
UploadStaging::UploadStaging(Render::VulkanContext* context, VkCommandBuffer commandBuffer, uint32_t stagingSize)
    : context(context), commandBuffer(commandBuffer), stagingBuffer(Render::AllocatedBuffer::CreateAllocatedStagingBuffer(context, stagingSize)), stagingAllocator(stagingSize)
{
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT, // Unsignaled
    };
    VK_CHECK(vkCreateFence(context->device, &fenceInfo, nullptr, &fence));
}

UploadStaging::~UploadStaging()
{
    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(context->device, fence, nullptr);
    }
}

void UploadStaging::StartCommandBuffer()
{
    if (bCommandBufferStarted) {
        return;
    }

    // Shouldn't happen, but just in case
    vkWaitForFences(context->device, 1, &fence, true, UINT64_MAX);

    VK_CHECK(vkResetFences(context->device, 1, &fence));
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

    const VkCommandBufferBeginInfo cmdBeginInfo = Render::VkHelpers::CommandBufferBeginInfo();
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBeginInfo));

    bCommandBufferStarted = true;
}

void UploadStaging::SubmitCommandBuffer()
{
    if (!bCommandBufferStarted) {
        SPDLOG_WARN("[UploadStaging::SubmitCommandBuffer] Command buffer not started");
        return;
    }
    VK_CHECK(vkEndCommandBuffer(commandBuffer));
    VkCommandBufferSubmitInfo cmdSubmitInfo = Render::VkHelpers::CommandBufferSubmitInfo(commandBuffer);
    const VkSubmitInfo2 submitInfo = Render::VkHelpers::SubmitInfo(&cmdSubmitInfo, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(context->transferQueue, 1, &submitInfo, fence));

    bCommandBufferStarted = false;
    stagingAllocator.Reset();
}

bool UploadStaging::IsReady() const
{
    return vkGetFenceStatus(context->device, fence) == VK_SUCCESS;
}

void UploadStaging::WaitForFence() const
{
    vkWaitForFences(context->device, 1, &fence, true, UINT64_MAX);
}
} // AssetLoad
