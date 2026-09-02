//
// Created by William on 2025-12-23.
//

#include "texture_load_slot.h"

#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "engine/compression/compression.h"
#include "engine/logging/engine_log.h"
#include "core/memory/memory_manager.h"
#include "engine/resources/texture/texture.h"
#include "platform/file_utils.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
TextureLoadSlot::TextureLoadSlot() = default;

TextureLoadSlot::~TextureLoadSlot()
{
    transferSubmit.Destroy(context);
}

void TextureLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, TextureSlotHandle textureSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);

    transferSubmit.Initialize(context, context->transferQueueFamily);
}

void TextureLoadSlot::Launch(
    TextureSlotHandle _textureSlotHandle,
    UploadStaging* _uploadStaging,
    Engine::Texture* _outputTexture)
{
    textureSlotHandle = _textureSlotHandle;
    uploadStaging = _uploadStaging;
    outputTexture = _outputTexture;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void TextureLoadSlot::Clear()
{
    textureSlotHandle = {};
    outputTexture = nullptr;
    uploadStaging = nullptr;

    blobData = {};
    blobView = {};
}

void TextureLoadSlot::LoadTextureTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadTextureFromDisk()) {
        loadSlot->_notifyCallback(false, loadSlot->textureSlotHandle);
        loadSlot->Clear();
        return;
    }

    AllocatedTextureResources resources = loadSlot->AllocateGPUResources();
    if (!resources.bSuccess) {
        loadSlot->_notifyCallback(false, loadSlot->textureSlotHandle);
        loadSlot->Clear();
        return;
    }

    loadSlot->outputTexture->image = std::move(resources.image);
    loadSlot->outputTexture->imageView = std::move(resources.imageView);

    VkCommandBuffer cmd = loadSlot->transferSubmit.cmd;
    VkFence fence = loadSlot->transferSubmit.fence;

    auto submitAndWait = [&](bool reset) {
        ZoneScopedN("SubmitAndWait");

        VK_CHECK(vkEndCommandBuffer(cmd));
        std::binary_semaphore done(0);
        loadSlot->_requestDispatchCallback(cmd, fence, &done);
        done.acquire();
        VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));

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
    VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));

    loadSlot->PostUploadSetup();

    loadSlot->transferSubmit.Reset(loadSlot->context);

    loadSlot->_notifyCallback(true, loadSlot->textureSlotHandle);
}

bool TextureLoadSlot::LoadTextureFromDisk()
{
    ZoneScopedN("LoadTextureFromDisk");

    if (!outputTexture) {
        LOG_ERROR(Asset, "Output texture is null");
        return false;
    }

    const Core::Path& texturePath = outputTexture->source; {
        ZoneScopedN("FileExistsCheck");
        if (!texturePath.Exists()) {
            LOG_ERROR(Asset, "Failed to find texture: {}", texturePath.c_str());
            return false;
        }
    } {
        ZoneScopedN("WImageParse");

        Platform::ScopedFileMapping map(texturePath, true);
        if (!map.data || outputTexture->dataOffset + outputTexture->dataSize > map.size) {
            LOG_ERROR(Asset, "Failed to read .wtexture data: {}", texturePath.c_str());
            return false;
        }

        blobData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetTexture, outputTexture->uncompressedSize);
        Engine::Decompress(outputTexture->compressionType, map.data + outputTexture->dataOffset, outputTexture->dataSize, blobData.Data(), outputTexture->uncompressedSize);

        if (!blobView.Parse(blobData.Data(), blobData.Size())) {
            LOG_ERROR(Asset, "Failed to parse texture image blob: {}", texturePath.c_str());
            return false;
        }
    }

    if (blobView.RowPitch(0) > uploadStaging->GetStagingAllocator().GetCapacity()) {
        LOG_ERROR(Asset, "Texture block row too large for staging buffer: {}", texturePath.c_str());
        return false;
    }

    if (blobView.bCubemap) {
        LOG_ERROR(Asset, "Cubemaps not supported: {}", texturePath.c_str());
        return false;
    }

    return true;
}

TextureLoadSlot::AllocatedTextureResources TextureLoadSlot::AllocateGPUResources() const
{
    AllocatedTextureResources output{};
    VkExtent3D extent{
        .width = blobView.baseWidth,
        .height = blobView.baseHeight,
        .depth = 1
    };

    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        blobView.vkFormat,
        extent,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.mipLevels = blobView.levelCount;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    output.image = Render::AllocatedImage::CreateAllocatedImage(context, context->ApplyImageSharing(imageCreateInfo, true));

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
        output.image.handle,
        output.image.format,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.subresourceRange.levelCount = blobView.levelCount;

    output.imageView = Render::ImageView::CreateImageView(context, viewInfo);
    output.bSuccess = true;
    return output;
}

void TextureLoadSlot::UploadTexture(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadTexture");

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    // Pre-copy barrier: UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputTexture->image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, blobView.levelCount, 0, 1),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &preCopyBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Upload all mip levels; mips larger than the staging buffer stream in block-row chunks
    for (uint32_t mipLevel = 0; mipLevel < blobView.levelCount; mipLevel++) {
        ZoneScopedN("Upload Mip");

        const uint8_t* mipData = blobView.FaceData(mipLevel, 0);
        uint32_t mipWidth = blobView.LevelWidth(mipLevel);
        uint32_t mipHeight = blobView.LevelHeight(mipLevel);
        size_t mipSize = blobView.FaceSize(mipLevel);
        const size_t rowPitch = blobView.RowPitch(mipLevel);
        const uint32_t totalRows = std::max(1u, static_cast<uint32_t>(mipSize / rowPitch));
        const uint32_t texelRowsPerRow = (mipHeight + totalRows - 1) / totalRows;

        uint32_t rowsDone = 0;
        while (rowsDone < totalRows) {
            uint32_t rowsFit = static_cast<uint32_t>(stagingAllocator.GetRemaining() / rowPitch);
            if (rowsFit == 0) {
                submitAndWait(true);
                stagingAllocator.Reset();
                rowsFit = static_cast<uint32_t>(stagingAllocator.GetRemaining() / rowPitch);
                assert(rowsFit > 0 && "Single block row too large for staging buffer");
            }
            rowsFit = std::min(rowsFit, totalRows - rowsDone);
            const size_t chunkBytes = static_cast<size_t>(rowsFit) * rowPitch;
            const size_t allocation = stagingAllocator.Allocate(chunkBytes, 16);
            if (allocation == SIZE_MAX) {
                submitAndWait(true);
                stagingAllocator.Reset();
                continue;
            }

            char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
            memcpy(stagingPtr, mipData + static_cast<size_t>(rowsDone) * rowPitch, chunkBytes);

            const uint32_t texelY = rowsDone * texelRowsPerRow;
            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = allocation;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = mipLevel;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageOffset = {0, static_cast<int32_t>(texelY), 0};
            copyRegion.imageExtent = {mipWidth, std::min(rowsFit * texelRowsPerRow, mipHeight - texelY), 1};

            vkCmdCopyBufferToImage(
                cmd,
                stagingBuffer.handle,
                outputTexture->image.handle,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &copyRegion
            );

            rowsDone += rowsFit;
        }
    }

    // Final barrier: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputTexture->image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, blobView.levelCount, 0, 1),
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    depInfo.pImageMemoryBarriers = &finalBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    blobData = {};
    blobView = {};
}

void TextureLoadSlot::PostUploadSetup()
{
    bool updateRes = resourceManager->bindlessSamplerTextureDescriptorBuffer.UpdateTexture(
        outputTexture->bindlessHandle, {
            .imageView = outputTexture->imageView.handle,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });

#ifdef ENABLE_VULKAN_VALIDATION
    VkDebugUtilsObjectNameInfoEXT nameInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    nameInfo.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
    nameInfo.objectHandle = reinterpret_cast<uint64_t>(outputTexture->imageView.handle);
    nameInfo.pObjectName = outputTexture->name.c_str();
    vkSetDebugUtilsObjectNameEXT(context->device, &nameInfo);

    VkDebugUtilsObjectNameInfoEXT viewNameInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    viewNameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
    viewNameInfo.objectHandle = reinterpret_cast<uint64_t>(outputTexture->image.handle);
    viewNameInfo.pObjectName = outputTexture->name.c_str();
    vkSetDebugUtilsObjectNameEXT(context->device, &viewNameInfo);
#endif

    if (!updateRes) {
        LOG_ERROR(Asset, "Failed to update bindless texture descriptor");
    }
}
} // AssetLoad
