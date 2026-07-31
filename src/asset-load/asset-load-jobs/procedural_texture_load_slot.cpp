//
// Created by William on 2026-05-21.
//

#include "procedural_texture_load_slot.h"

#include <cmath>
#include <tracy/Tracy.hpp>

#include "engine/resources/texture/texture.h"
#include "render/descriptors/procedural_texture_generate_resources.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/shaders/constants_interop.h"
#include "render/shaders/push_constant_interop.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
void ProceduralTextureLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    Render::PipelineManager* _pipelineManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, ProceduralTextureSlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    pipelineManager = _pipelineManager;
    _dispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);
}

void ProceduralTextureLoadSlot::Launch(ProceduralTextureSlotHandle handle, Engine::Texture* texture, StringID _pipelineId)
{
    slotHandle = handle;
    outputTexture = texture;
    pipelineId = _pipelineId;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void ProceduralTextureLoadSlot::Clear()
{
    slotHandle = {};
    outputTexture = nullptr;
    pipelineId = {};
}

void ProceduralTextureLoadSlot::GenerateTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    ZoneScopedN("ProceduralTextureLoadSlot::GenerateTask");

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(loadSlot->context->graphicsQueueFamily);
    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(loadSlot->context->device, &poolInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, commandPool);
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(loadSlot->context->device, &cmdInfo, &cmd));

    VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    VK_CHECK(vkCreateFence(loadSlot->context->device, &fenceInfo, nullptr, &fence));

    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    auto submitAndWait = [&](bool restart) {
        VK_CHECK(vkEndCommandBuffer(cmd));
        std::binary_semaphore done(0);
        loadSlot->_dispatchCallback(cmd, fence, &done);
        done.acquire();
        VK_CHECK(vkResetFences(loadSlot->context->device, 1, &fence));
        VK_CHECK(vkResetCommandBuffer(cmd, 0));
        if (restart) {
            VkCommandBufferBeginInfo restartInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            VK_CHECK(vkBeginCommandBuffer(cmd, &restartInfo));
        }
    };

    const Render::PipelineEntry entry = loadSlot->pipelineManager->GetPipelineEntrySnapshot(loadSlot->pipelineId);
    if (entry.pipeline == VK_NULL_HANDLE) {
        SPDLOG_ERROR("ProceduralTextureLoadSlot: pipeline {:x} not ready", loadSlot->pipelineId.id);
        vkDestroyFence(loadSlot->context->device, fence, nullptr);
        vkDestroyCommandPool(loadSlot->context->device, commandPool, nullptr);
        loadSlot->_notifyCallback(false, loadSlot->slotHandle);
        loadSlot->Clear();
        return;
    }

    Engine::Texture* tex = loadSlot->outputTexture;
    const uint32_t width = tex->width;
    const uint32_t height = tex->height;
    const uint32_t mipCount = tex->mipCount;
    const VkFormat format = tex->format;

    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        format,
        {width, height, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    imageCreateInfo.mipLevels = mipCount;
    tex->image = Render::AllocatedImage::CreateAllocatedImage(loadSlot->context, imageCreateInfo);

    VkImageViewCreateInfo sampledViewInfo = Render::VkHelpers::ImageViewCreateInfo(tex->image.handle, format, VK_IMAGE_ASPECT_COLOR_BIT);
    sampledViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1);
    tex->imageView = Render::ImageView::CreateImageView(loadSlot->context, sampledViewInfo);

    // Storage view for mip 0, write target for the compute shader
    VkImageViewCreateInfo storageViewInfo = Render::VkHelpers::ImageViewCreateInfo(tex->image.handle, format, VK_IMAGE_ASPECT_COLOR_BIT);
    storageViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
    Render::ImageView storageView = Render::ImageView::CreateImageView(loadSlot->context, storageViewInfo);

    // Bind this slot's index in the procedural descriptor
    uint32_t outputIndex = loadSlot->slotHandle.index;
    bool res = loadSlot->resourceManager->proceduralTextureGenerateResources.SetRWTexture({nullptr, storageView.handle, VK_IMAGE_LAYOUT_GENERAL}, outputIndex);

    // Transition mip 0: UNDEFINED -> GENERAL
    {
        VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
            tex->image.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
        );
        VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    VkDescriptorBufferBindingInfoEXT binding = loadSlot->resourceManager->proceduralTextureGenerateResources.GetBindingInfo();
    uint32_t bindingIndex = 0;
    VkDeviceSize bindingOffset = 0;
    vkCmdBindDescriptorBuffersEXT(cmd, 1, &binding);
    vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, entry.layout, 0, 1, &bindingIndex, &bindingOffset);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, entry.pipeline);
    ProceduralTextureBasePushConstant pc{outputIndex};
    vkCmdPushConstants(cmd, entry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ProceduralTextureBasePushConstant), &pc);

    uint32_t gx = (width + PROCEDURAL_TEXTURE_DISPATCH_X - 1) / PROCEDURAL_TEXTURE_DISPATCH_X;
    uint32_t gy = (height + PROCEDURAL_TEXTURE_DISPATCH_Y - 1) / PROCEDURAL_TEXTURE_DISPATCH_Y;
    vkCmdDispatch(cmd, gx, gy, 1);

    if (mipCount > 1) {
        // Transition mip 0: GENERAL -> TRANSFER_SRC_OPTIMAL
        {
            VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
                tex->image.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
            vkCmdPipelineBarrier2(cmd, &dep);
        }

        for (uint32_t mip = 1; mip < mipCount; mip++) {
            VkImageMemoryBarrier2 barriers[2];
            barriers[0] = Render::VkHelpers::ImageMemoryBarrier(
                tex->image.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, 1),
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            barriers[1] = Render::VkHelpers::ImageMemoryBarrier(
                tex->image.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1),
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );
            VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers};
            vkCmdPipelineBarrier2(cmd, &dep);

            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {std::max(1, static_cast<int32_t>(width) >> (mip - 1)), std::max(1, static_cast<int32_t>(height) >> (mip - 1)), 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {std::max(1, static_cast<int32_t>(width) >> mip), std::max(1, static_cast<int32_t>(height) >> mip), 1};
            vkCmdBlitImage(cmd, tex->image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex->image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            VkImageMemoryBarrier2 srcBarrier = Render::VkHelpers::ImageMemoryBarrier(
                tex->image.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1),
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            );
            VkDependencyInfo srcDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &srcBarrier};
            vkCmdPipelineBarrier2(cmd, &srcDep);
        }

        // All mips in TRANSFER_SRC_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
        {
            VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
                tex->image.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1),
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
            vkCmdPipelineBarrier2(cmd, &dep);
        }
    }
    else {
        // Single mip: GENERAL -> SHADER_READ_ONLY_OPTIMAL
        VkImageMemoryBarrier2 barrier = Render::VkHelpers::ImageMemoryBarrier(
            tex->image.handle,
            Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    submitAndWait(false);

    storageView = {};

    loadSlot->PostGenerateSetup();

    vkDestroyFence(loadSlot->context->device, fence, nullptr);
    vkDestroyCommandPool(loadSlot->context->device, commandPool, nullptr);

    loadSlot->_notifyCallback(true, loadSlot->slotHandle);
}

void ProceduralTextureLoadSlot::PostGenerateSetup()
{
    resourceManager->bindlessSamplerTextureDescriptorBuffer.UpdateTexture(
        outputTexture->bindlessHandle, {
            .imageView = outputTexture->imageView.handle,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });
}
} // AssetLoad
