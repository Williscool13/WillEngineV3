//
// Created by William on 2026-02-16.
//

#include "cubemap_load_slot.h"

#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "ktxvulkan.h"
#include "render/types/cubemap_asset.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
CubemapLoadSlot::CubemapLoadSlot() = default;

CubemapLoadSlot::~CubemapLoadSlot() = default;

void CubemapLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    std::function<void(bool success, CubemapSlotHandle cubemapSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);
}

void CubemapLoadSlot::Launch(
    CubemapSlotHandle _cubemapSlotHandle,
    UploadStagingSlotHandle _uploadStagingSlotHandle,
    UploadStaging* _uploadStaging,
    Render::Cubemap* _outputCubemap)
{
    cubemapSlotHandle = _cubemapSlotHandle;
    uploadStagingSlotHandle = _uploadStagingSlotHandle;
    uploadStaging = _uploadStaging;
    outputCubemap = _outputCubemap;

    if (task && !task->GetIsComplete()) {
        scheduler->WaitforTask(task.get());
    }
    task = std::make_unique<LoadCubemapTask>();
    task->loadSlot = this;
    scheduler->AddTaskSetToPipe(task.get());
}

void CubemapLoadSlot::Clear()
{
    cubemapSlotHandle = {};
    uploadStagingSlotHandle = {};
    outputCubemap = nullptr;
    uploadStaging = nullptr;

    if (texture) {
        ktxTexture2_Destroy(texture);
        texture = nullptr;
    }
}

void CubemapLoadSlot::LoadCubemapTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadCubemapFromDisk()) {
        loadSlot->_notifyCallback(false, loadSlot->cubemapSlotHandle, loadSlot->uploadStagingSlotHandle);
        loadSlot->Clear();
        return;
    }

    if (!loadSlot->AllocateGPUResources()) {
        loadSlot->_notifyCallback(false, loadSlot->cubemapSlotHandle, loadSlot->uploadStagingSlotHandle);
        loadSlot->Clear();
        return;
    }

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(loadSlot->context->transferQueueFamily);
    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(loadSlot->context->device, &poolInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(1, commandPool);
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(loadSlot->context->device, &cmdInfo, &cmd));

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    VK_CHECK(vkCreateFence(loadSlot->context->device, &fenceInfo, nullptr, &fence));

    auto submitAndWait = [&](bool reset) {
        ZoneScopedN("SubmitAndWait");

        VK_CHECK(vkEndCommandBuffer(cmd));
        std::binary_semaphore done(0);
        loadSlot->_requestDispatchCallback(cmd, fence, &done);
        done.acquire();

        if (reset) {
            VK_CHECK(vkResetFences(loadSlot->context->device, 1, &fence));
            VK_CHECK(vkResetCommandBuffer(cmd, 0));

            VkCommandBufferBeginInfo beginInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            };
            VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
        }
    };

    VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,};
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    loadSlot->UploadCubemap(cmd, submitAndWait);

    VK_CHECK(vkEndCommandBuffer(cmd));
    std::binary_semaphore done(0);
    loadSlot->_requestDispatchCallback(cmd, fence, &done);
    done.acquire();

    loadSlot->PostUploadSetup();

    vkDestroyFence(loadSlot->context->device, fence, nullptr);
    vkDestroyCommandPool(loadSlot->context->device, commandPool, nullptr);

    loadSlot->_notifyCallback(true, loadSlot->cubemapSlotHandle, loadSlot->uploadStagingSlotHandle);
}

bool CubemapLoadSlot::LoadCubemapFromDisk()
{
    ZoneScopedN("LoadCubemapFromDisk");

    if (!outputCubemap) {
        SPDLOG_ERROR("Output cubemap is null");
        return false;
    }

    const std::filesystem::path& cubemapPath = outputCubemap->source;

    {
        ZoneScopedN("FileExistsCheck");
        if (!std::filesystem::exists(cubemapPath)) {
            SPDLOG_ERROR("Failed to find cubemap: {}", cubemapPath.string());
            return false;
        }
    }

    {
        ZoneScopedN("KTXCreateFromFile");
        ktx_error_code_e result = ktxTexture2_CreateFromNamedFile(
            cubemapPath.string().c_str(),
            KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
            &texture
        );

        if (result != KTX_SUCCESS) {
            SPDLOG_ERROR("Failed to load KTX cubemap: {}", cubemapPath.string());
            return false;
        }
    }

    assert(!ktxTexture2_NeedsTranscoding(texture) && "This engine no longer supports UASTC/ETC1S compressed textures");

    if (!texture->isCubemap) {
        SPDLOG_ERROR("Expected cubemap texture: {}", cubemapPath.string());
        return false;
    }

    if (texture->numFaces != 6) {
        SPDLOG_ERROR("Cubemap must have 6 faces: {}", cubemapPath.string());
        return false;
    }

    // Size check for single face mip0
    ktx_size_t mip0Size = ktxTexture_GetImageSize(ktxTexture(texture), 0);
    if (mip0Size > TEXTURE_LOAD_STAGING_SIZE) {
        SPDLOG_ERROR("Cubemap face too large for staging buffer: {}", cubemapPath.string());
        return false;
    }

    return true;
}

bool CubemapLoadSlot::AllocateGPUResources()
{
    VkExtent3D extent{
        .width = texture->baseWidth,
        .height = texture->baseHeight,
        .depth = 1
    };

    VkFormat imageFormat = ktxTexture2_GetVkFormat(texture);
    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        imageFormat,
        extent,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.mipLevels = texture->numLevels;
    imageCreateInfo.arrayLayers = 6;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    outputCubemap->image = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
        outputCubemap->image.handle,
        outputCubemap->image.format,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.subresourceRange.layerCount = 6;
    viewInfo.subresourceRange.levelCount = texture->numLevels;

    outputCubemap->imageView = Render::ImageView::CreateImageView(context, viewInfo);

    return true;
}

void CubemapLoadSlot::UploadCubemap(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadCubemap");

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    // Pre-copy barrier: UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputCubemap->image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->numLevels, 0, 6),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &preCopyBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Upload all mip levels for all 6 faces
    for (uint32_t mipLevel = 0; mipLevel < texture->numLevels; mipLevel++) {
        uint32_t mipWidth = std::max(1u, texture->baseWidth >> mipLevel);
        uint32_t mipHeight = std::max(1u, texture->baseHeight >> mipLevel);
        size_t mipSize = ktxTexture_GetImageSize(ktxTexture(texture), mipLevel);

        for (uint32_t face = 0; face < 6; face++) {
            ZoneScopedN("Upload Face");

            size_t faceOffset;
            ktxTexture_GetImageOffset(ktxTexture(texture), mipLevel, 0, face, &faceOffset);

            size_t allocation = stagingAllocator.Allocate(mipSize, 16);
            if (allocation == SIZE_MAX) {
                submitAndWait(true);
                stagingAllocator.Reset();
                allocation = stagingAllocator.Allocate(mipSize, 16);
                assert(allocation != SIZE_MAX && "Cubemap face too large for staging buffer");
            }

            char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
            memcpy(stagingPtr, texture->pData + faceOffset, mipSize);

            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = allocation;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = mipLevel;
            copyRegion.imageSubresource.baseArrayLayer = face;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageOffset = {0, 0, 0};
            copyRegion.imageExtent = {mipWidth, mipHeight, 1};

            vkCmdCopyBufferToImage(
                cmd,
                stagingBuffer.handle,
                outputCubemap->image.handle,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copyRegion
            );
        }
    }

    // Final barrier: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL with queue family transfer
    VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputCubemap->image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->numLevels, 0, 6),
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    finalBarrier.srcQueueFamilyIndex = context->transferQueueFamily;
    finalBarrier.dstQueueFamilyIndex = context->graphicsQueueFamily;

    depInfo.pImageMemoryBarriers = &finalBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    outputCubemap->acquireBarrier = Render::VkHelpers::FromVkBarrier(finalBarrier);

    ktxTexture2_Destroy(texture);
    texture = nullptr;
}

void CubemapLoadSlot::PostUploadSetup()
{
    bool updateRes = resourceManager->bindlessSamplerTextureDescriptorBuffer.UpdateCubemap(
        outputCubemap->bindlessHandle, {
            .imageView = outputCubemap->imageView.handle,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });

    if (!updateRes) {
        SPDLOG_ERROR("Failed to update bindless cubemap descriptor");
    }
}
} // AssetLoad