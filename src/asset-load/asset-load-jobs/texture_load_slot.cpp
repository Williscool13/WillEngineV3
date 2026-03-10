//
// Created by William on 2025-12-23.
//

#include "texture_load_slot.h"

#include <fstream>
#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "engine/logging/engine_log.h"
#include "engine/textures/texture.h"
#include "ktxvulkan.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
TextureLoadSlot::TextureLoadSlot() = default;

TextureLoadSlot::~TextureLoadSlot() = default;

void TextureLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    std::function<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    std::function<void(bool success, TextureSlotHandle textureSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);
}

void TextureLoadSlot::Launch(
    TextureSlotHandle _textureSlotHandle,
    UploadStagingSlotHandle _uploadStagingSlotHandle,
    UploadStaging* _uploadStaging,
    Engine::Texture* _outputTexture)
{
    textureSlotHandle = _textureSlotHandle;
    uploadStagingSlotHandle = _uploadStagingSlotHandle;
    uploadStaging = _uploadStaging;
    outputTexture = _outputTexture;

    if (task && !task->GetIsComplete()) {
        scheduler->WaitforTask(task.get());
    }
    task = std::make_unique<LoadTextureTask>();
    task->loadSlot = this;
    scheduler->AddTaskSetToPipe(task.get());
}

void TextureLoadSlot::Clear()
{
    textureSlotHandle = {};
    uploadStagingSlotHandle = {};
    outputTexture = nullptr;
    uploadStaging = nullptr;

    if (texture) {
        ktxTexture2_Destroy(texture);
        texture = nullptr;
    }
}

void TextureLoadSlot::LoadTextureTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadTextureFromDisk()) {
        loadSlot->_notifyCallback(false, loadSlot->textureSlotHandle, loadSlot->uploadStagingSlotHandle);
        loadSlot->Clear();
        return;
    }

    if (!loadSlot->AllocateGPUResources()) {
        loadSlot->_notifyCallback(false, loadSlot->textureSlotHandle, loadSlot->uploadStagingSlotHandle);
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

    loadSlot->UploadTexture(cmd, submitAndWait);

    VK_CHECK(vkEndCommandBuffer(cmd));
    std::binary_semaphore done(0);
    loadSlot->_requestDispatchCallback(cmd, fence, &done);
    done.acquire();

    loadSlot->PostUploadSetup();

    vkDestroyFence(loadSlot->context->device, fence, nullptr);
    vkDestroyCommandPool(loadSlot->context->device, commandPool, nullptr);

    loadSlot->_notifyCallback(true, loadSlot->textureSlotHandle, loadSlot->uploadStagingSlotHandle);
}

bool TextureLoadSlot::LoadTextureFromDisk()
{
    ZoneScopedN("LoadTextureFromDisk");

    if (!outputTexture) {
        LOG_ERROR(Asset, "Output texture is null");
        return false;
    }

    const std::filesystem::path& texturePath = outputTexture->source;

    {
        ZoneScopedN("FileExistsCheck");
        if (!std::filesystem::exists(texturePath)) {
            LOG_ERROR(Asset, "Failed to find texture: {}", texturePath.string());
            return false;
        }
    }

    {
        ZoneScopedN("KTXCreateFromFile");
        ktx_error_code_e result;

        std::ifstream f(texturePath, std::ios::binary);
        std::vector<uint8_t> blob(outputTexture->dataSize);
        f.seekg(outputTexture->dataOffset);
        f.read(reinterpret_cast<char*>(blob.data()), static_cast<std::streamsize>(outputTexture->dataSize));
        if (!f) {
            LOG_ERROR(Asset, "Failed to read .wtexture data: {}", texturePath.string());
            return false;
        }

        result = ktxTexture2_CreateFromMemory(blob.data(), blob.size(),
                                              KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);

        if (result != KTX_SUCCESS) {
            LOG_ERROR(Asset, "Failed to load KTX texture: {}", texturePath.string());
            return false;
        }
    }

    assert(!ktxTexture2_NeedsTranscoding(texture) && "This engine no longer supports UASTC/ETC1S compressed textures");

    ktx_size_t mip0Size = ktxTexture_GetImageSize(ktxTexture(texture), 0);
    if (mip0Size > TEXTURE_LOAD_STAGING_SIZE) {
        LOG_ERROR(Asset, "Texture too large for staging buffer: {}", texturePath.string());
        return false;
    }

    if (texture->numDimensions != 2) {
        LOG_ERROR(Asset, "Only 2D textures supported: {}", texturePath.string());
        return false;
    }

    if (texture->isArray) {
        LOG_ERROR(Asset, "Texture arrays not supported: {}", texturePath.string());
        return false;
    }

    if (texture->isCubemap) {
        LOG_ERROR(Asset, "Cubemaps not supported: {}", texturePath.string());
        return false;
    }

    return true;
}

bool TextureLoadSlot::AllocateGPUResources()
{
    VkExtent3D extent{
        .width = texture->baseWidth,
        .height = texture->baseHeight,
        .depth = texture->baseDepth
    };

    VkFormat imageFormat = ktxTexture2_GetVkFormat(texture);
    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        imageFormat,
        extent,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.mipLevels = texture->numLevels;
    imageCreateInfo.arrayLayers = texture->numLayers;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    outputTexture->image = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
        outputTexture->image.handle,
        outputTexture->image.format,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.layerCount = texture->numLayers;
    viewInfo.subresourceRange.levelCount = texture->numLevels;

    outputTexture->imageView = Render::ImageView::CreateImageView(context, viewInfo);

    return true;
}

void TextureLoadSlot::UploadTexture(VkCommandBuffer cmd, const std::function<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadTexture");

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    // Pre-copy barrier: UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputTexture->image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->numLevels, 0, texture->numLayers),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &preCopyBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Upload all mip levels
    for (uint32_t mipLevel = 0; mipLevel < texture->numLevels; mipLevel++) {
        ZoneScopedN("Upload Mip");

        size_t mipOffset;
        ktxTexture_GetImageOffset(ktxTexture(texture), mipLevel, 0, 0, &mipOffset);
        uint32_t mipWidth = std::max(1u, texture->baseWidth >> mipLevel);
        uint32_t mipHeight = std::max(1u, texture->baseHeight >> mipLevel);
        uint32_t mipDepth = std::max(1u, texture->baseDepth >> mipLevel);
        size_t mipSize = ktxTexture_GetImageSize(ktxTexture(texture), mipLevel);

        size_t allocation = stagingAllocator.Allocate(mipSize, 16);
        if (allocation == SIZE_MAX) {
            submitAndWait(true);
            stagingAllocator.Reset();
            allocation = stagingAllocator.Allocate(mipSize, 16);
            assert(allocation != SIZE_MAX && "Mip level too large for staging buffer");
        }

        char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
        memcpy(stagingPtr, texture->pData + mipOffset, mipSize);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = allocation;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = mipLevel;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = texture->numLayers;
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {mipWidth, mipHeight, mipDepth};

        vkCmdCopyBufferToImage(
            cmd,
            stagingBuffer.handle,
            outputTexture->image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion
        );
    }

    // Final barrier: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL with queue family transfer
    VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputTexture->image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->numLevels, 0, texture->numLayers),
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    finalBarrier.srcQueueFamilyIndex = context->transferQueueFamily;
    finalBarrier.dstQueueFamilyIndex = context->graphicsQueueFamily;

    depInfo.pImageMemoryBarriers = &finalBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    outputTexture->acquireBarrier = Render::VkHelpers::FromVkBarrier(finalBarrier);

    ktxTexture2_Destroy(texture);
    texture = nullptr;
}

void TextureLoadSlot::PostUploadSetup()
{
    bool updateRes = resourceManager->bindlessSamplerTextureDescriptorBuffer.UpdateTexture(
        outputTexture->bindlessHandle, {
            .imageView = outputTexture->imageView.handle,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });

    if (!updateRes) {
        LOG_ERROR(Asset, "Failed to update bindless texture descriptor");
    }
}
} // AssetLoad