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
ProceduralTextureLoadSlot::~ProceduralTextureLoadSlot()
{
    computeSubmit.Destroy(context);
}

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

    const uint32_t submitFamily = context->computeQueue != VK_NULL_HANDLE ? context->computeQueueFamily : context->graphicsQueueFamily;
    computeSubmit.Initialize(context, submitFamily);
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

    VkCommandBuffer cmd = loadSlot->computeSubmit.cmd;
    VkFence fence = loadSlot->computeSubmit.fence;

    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    auto submitAndWait = [&](bool restart) {
        VK_CHECK(vkEndCommandBuffer(cmd));
        std::binary_semaphore done(0);
        loadSlot->_dispatchCallback(cmd, fence, &done);
        done.acquire();
        VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));
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
        loadSlot->computeSubmit.Reset(loadSlot->context);
        loadSlot->_notifyCallback(false, loadSlot->slotHandle);
        loadSlot->Clear();
        return;
    }

    Engine::Texture* tex = loadSlot->outputTexture;
    const uint32_t width = tex->width;
    const uint32_t height = tex->height;
    const uint32_t mipCount = tex->mipCount;
    const VkFormat format = tex->format;
    assert(mipCount <= Render::ProceduralTextureGenerateResources::MAX_MIPS_PER_SLOT);

    const Render::PipelineEntry downsampleEntry = loadSlot->pipelineManager->GetPipelineEntrySnapshot(SID("procedural_mip_downsample"));
    if (mipCount > 1 && downsampleEntry.pipeline == VK_NULL_HANDLE) {
        SPDLOG_ERROR("ProceduralTextureLoadSlot: procedural_mip_downsample pipeline not ready");
        loadSlot->computeSubmit.Reset(loadSlot->context);
        loadSlot->_notifyCallback(false, loadSlot->slotHandle);
        loadSlot->Clear();
        return;
    }

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

    Core::Array<Render::ImageView, Render::ProceduralTextureGenerateResources::MAX_MIPS_PER_SLOT> storageViews{};
    const uint32_t baseIndex = loadSlot->slotHandle.index * Render::ProceduralTextureGenerateResources::MAX_MIPS_PER_SLOT;
    for (uint32_t mip = 0; mip < mipCount; mip++) {
        VkImageViewCreateInfo storageViewInfo = Render::VkHelpers::ImageViewCreateInfo(tex->image.handle, format, VK_IMAGE_ASPECT_COLOR_BIT);
        storageViewInfo.subresourceRange = Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1);
        storageViews[mip] = Render::ImageView::CreateImageView(loadSlot->context, storageViewInfo);
        loadSlot->resourceManager->proceduralTextureGenerateResources.SetRWTexture({nullptr, storageViews[mip].handle, VK_IMAGE_LAYOUT_GENERAL}, baseIndex + mip);
    }

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
    ProceduralTextureBasePushConstant pc{baseIndex};
    vkCmdPushConstants(cmd, entry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ProceduralTextureBasePushConstant), &pc);

    uint32_t gx = (width + PROCEDURAL_TEXTURE_DISPATCH_X - 1) / PROCEDURAL_TEXTURE_DISPATCH_X;
    uint32_t gy = (height + PROCEDURAL_TEXTURE_DISPATCH_Y - 1) / PROCEDURAL_TEXTURE_DISPATCH_Y;
    vkCmdDispatch(cmd, gx, gy, 1);

    if (mipCount > 1) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, downsampleEntry.pipeline);
        // Push constant ranges differ from the generate pipeline, so descriptor offsets must be rebound for this layout
        vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, downsampleEntry.layout, 0, 1, &bindingIndex, &bindingOffset);

        for (uint32_t mip = 1; mip < mipCount; mip++) {
            VkImageMemoryBarrier2 barriers[2];
            barriers[0] = Render::VkHelpers::ImageMemoryBarrier(
                tex->image.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, 1),
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            barriers[1] = Render::VkHelpers::ImageMemoryBarrier(
                tex->image.handle,
                Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1),
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL
            );
            VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers};
            vkCmdPipelineBarrier2(cmd, &dep);

            ProceduralMipDownsamplePushConstant downsamplePC{baseIndex + mip - 1, baseIndex + mip};
            vkCmdPushConstants(cmd, downsampleEntry.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ProceduralMipDownsamplePushConstant), &downsamplePC);
            const uint32_t mipWidth = std::max(1u, width >> mip);
            const uint32_t mipHeight = std::max(1u, height >> mip);
            vkCmdDispatch(cmd, (mipWidth + PROCEDURAL_TEXTURE_DISPATCH_X - 1) / PROCEDURAL_TEXTURE_DISPATCH_X, (mipHeight + PROCEDURAL_TEXTURE_DISPATCH_Y - 1) / PROCEDURAL_TEXTURE_DISPATCH_Y, 1);
        }
    }

    // All mips GENERAL -> SHADER_READ_ONLY_OPTIMAL; queue family release to graphics when generated on a separate compute family
    VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
        tex->image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1),
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    const uint32_t submitFamily = loadSlot->context->computeQueue != VK_NULL_HANDLE ? loadSlot->context->computeQueueFamily : loadSlot->context->graphicsQueueFamily;
    if (submitFamily != loadSlot->context->graphicsQueueFamily) {
        finalBarrier.srcQueueFamilyIndex = submitFamily;
        finalBarrier.dstQueueFamilyIndex = loadSlot->context->graphicsQueueFamily;
        tex->acquireBarrier = Render::VkHelpers::FromVkBarrier(finalBarrier);
    }
    else {
        finalBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        finalBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    }
    VkDependencyInfo finalDep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &finalBarrier};
    vkCmdPipelineBarrier2(cmd, &finalDep);

    submitAndWait(false);

    loadSlot->PostGenerateSetup();

    loadSlot->computeSubmit.Reset(loadSlot->context);

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
