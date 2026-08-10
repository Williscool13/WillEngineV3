//
// Created by William on 2025-12-23.
//

#include "texture_load_slot.h"

#include <fstream>
#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "engine/compression/compression.h"
#include "engine/logging/engine_log.h"
#include "ktxvulkan.h"
#include "core/memory/memory_manager.h"
#include "engine/resources/texture/texture.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
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
    Core::MemoryManager* _memoryManager,
    SubmitContextDepot* _submitDepot,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, TextureSlotHandle textureSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    submitDepot = _submitDepot;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);
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

    if (texture) {
        ktxTexture2_Destroy(texture);
        texture = nullptr;
    }
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

    SubmitContext* submitContext = loadSlot->submitDepot->CheckOut(loadSlot->context->transferQueueFamily);
    VkCommandBuffer cmd = submitContext->cmd;
    VkFence fence = submitContext->fence;

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

    loadSlot->submitDepot->Return(submitContext);

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
        ZoneScopedN("KTXCreateFromFile");
        ktx_error_code_e result;

        std::ifstream f(texturePath.c_str(), std::ios::binary);

        Core::HeapArray<uint8_t> compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetTexture, outputTexture->dataSize);
        f.seekg(outputTexture->dataOffset);
        f.read(reinterpret_cast<char*>(compressed.Data()), static_cast<std::streamsize>(outputTexture->dataSize));
        if (!f) {
            LOG_ERROR(Asset, "Failed to read .wtexture data: {}", texturePath.c_str());
            return false;
        }

        // todo hand roll texture generation from ktx instead of using ktx functions which heap alloc
        Core::HeapArray<uint8_t> decompressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetTexture, outputTexture->uncompressedSize);
        Engine::Decompress(outputTexture->compressionType, compressed.Data(), compressed.Size(), decompressed.Data(), outputTexture->uncompressedSize);
        result = ktxTexture2_CreateFromMemory(decompressed.Data(), decompressed.Size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);

        if (result != KTX_SUCCESS) {
            LOG_ERROR(Asset, "Failed to load KTX texture: {}", texturePath.c_str());
            return false;
        }
    }

    assert(!ktxTexture2_NeedsTranscoding(texture) && "This engine no longer supports UASTC/ETC1S compressed textures");

    if (ktxTexture_GetRowPitch(ktxTexture(texture), 0) > uploadStaging->GetStagingAllocator().GetCapacity()) {
        LOG_ERROR(Asset, "Texture block row too large for staging buffer: {}", texturePath.c_str());
        return false;
    }

    if (texture->numDimensions != 2) {
        LOG_ERROR(Asset, "Only 2D textures supported: {}", texturePath.c_str());
        return false;
    }

    if (texture->isArray) {
        LOG_ERROR(Asset, "Texture arrays not supported: {}", texturePath.c_str());
        return false;
    }

    if (texture->isCubemap) {
        LOG_ERROR(Asset, "Cubemaps not supported: {}", texturePath.c_str());
        return false;
    }

    return true;
}

TextureLoadSlot::AllocatedTextureResources TextureLoadSlot::AllocateGPUResources() const
{
    AllocatedTextureResources output{};
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

    output.image = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
        output.image.handle,
        output.image.format,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.layerCount = texture->numLayers;
    viewInfo.subresourceRange.levelCount = texture->numLevels;

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
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->numLevels, 0, texture->numLayers),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &preCopyBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Upload all mip levels; mips larger than the staging buffer stream in block-row chunks
    for (uint32_t mipLevel = 0; mipLevel < texture->numLevels; mipLevel++) {
        ZoneScopedN("Upload Mip");

        size_t mipOffset;
        ktxTexture_GetImageOffset(ktxTexture(texture), mipLevel, 0, 0, &mipOffset);
        uint32_t mipWidth = std::max(1u, texture->baseWidth >> mipLevel);
        uint32_t mipHeight = std::max(1u, texture->baseHeight >> mipLevel);
        uint32_t mipDepth = std::max(1u, texture->baseDepth >> mipLevel);
        size_t mipSize = ktxTexture_GetImageSize(ktxTexture(texture), mipLevel);
        const size_t rowPitch = ktxTexture_GetRowPitch(ktxTexture(texture), mipLevel);
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
            memcpy(stagingPtr, texture->pData + mipOffset + static_cast<size_t>(rowsDone) * rowPitch, chunkBytes);

            const uint32_t texelY = rowsDone * texelRowsPerRow;
            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = allocation;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = mipLevel;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = texture->numLayers;
            copyRegion.imageOffset = {0, static_cast<int32_t>(texelY), 0};
            copyRegion.imageExtent = {mipWidth, std::min(rowsFit * texelRowsPerRow, mipHeight - texelY), mipDepth};

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
