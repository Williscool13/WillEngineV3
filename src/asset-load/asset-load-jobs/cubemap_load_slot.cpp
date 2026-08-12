//
// Created by William on 2026-02-16.
//

#include "cubemap_load_slot.h"

#include <fstream>
#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "core/containers/heap_array.h"
#include "core/memory/memory_manager.h"
#include "engine/compression/compression.h"
#include "engine/resources/environment_map/environment_map_format.h"
#include "render/resource_manager.h"
#include "render/types/cubemap_asset.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
CubemapLoadSlot::CubemapLoadSlot() = default;

CubemapLoadSlot::~CubemapLoadSlot()
{
    transferSubmit.Destroy(context);
}

void CubemapLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, CubemapSlotHandle cubemapSlotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);

    transferSubmit.Initialize(context, context->transferQueueFamily);
}

void CubemapLoadSlot::Launch(
    CubemapSlotHandle _cubemapSlotHandle,
    UploadStaging* _uploadStaging,
    Render::Cubemap* _outputCubemap)
{
    cubemapSlotHandle = _cubemapSlotHandle;
    uploadStaging = _uploadStaging;
    outputCubemap = _outputCubemap;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void CubemapLoadSlot::Clear()
{
    cubemapSlotHandle = {};
    outputCubemap = nullptr;
    uploadStaging = nullptr;

    blobData = {};
    blobView = {};
}

void CubemapLoadSlot::LoadCubemapTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadCubemapFromDisk()) {
        loadSlot->_notifyCallback(false, loadSlot->cubemapSlotHandle);
        loadSlot->Clear();
        return;
    }

    if (!loadSlot->AllocateGPUResources()) {
        loadSlot->_notifyCallback(false, loadSlot->cubemapSlotHandle);
        loadSlot->Clear();
        return;
    }

    VkCommandBuffer cmd = loadSlot->transferSubmit.cmd;
    VkFence fence = loadSlot->transferSubmit.fence;

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

    loadSlot->transferSubmit.Reset(loadSlot->context);

    loadSlot->_notifyCallback(true, loadSlot->cubemapSlotHandle);
}

bool CubemapLoadSlot::LoadCubemapFromDisk()
{
    ZoneScopedN("LoadCubemapFromDisk");

    if (!outputCubemap) {
        SPDLOG_ERROR("Output cubemap is null");
        return false;
    }

    const Core::Path& cubemapPath = outputCubemap->source;

    {
        ZoneScopedN("FileExistsCheck");
        if (!cubemapPath.Exists()) {
            SPDLOG_ERROR("Failed to find cubemap: {}", cubemapPath.c_str());
            return false;
        }
    }

    {
        ZoneScopedN("WImageLoad");

        Core::HeapArray<uint8_t> compressed = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetTexture, outputCubemap->dataSize);
        {
            ZoneScopedN("FileRead");
            std::ifstream f(cubemapPath.c_str(), std::ios::binary);
            f.seekg(static_cast<std::streamoff>(outputCubemap->dataOffset));
            f.read(reinterpret_cast<char*>(compressed.Data()), static_cast<std::streamsize>(outputCubemap->dataSize));
            if (!f) {
                SPDLOG_ERROR("Failed to read .wenvmap data: {}", cubemapPath.c_str());
                return false;
            }
        }

        blobData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetTexture, outputCubemap->uncompressedSize);
        {
            ZoneScopedN("Decompress");
            Engine::Decompress(outputCubemap->compressionType, compressed.Data(), compressed.Size(), blobData.Data(), outputCubemap->uncompressedSize);
        }
        {
            ZoneScopedN("WImageParse");
            if (!blobView.Parse(blobData.Data(), blobData.Size())) {
                SPDLOG_ERROR("Failed to parse cubemap image blob: {}", cubemapPath.c_str());
                return false;
            }
        }
    }

    if (!blobView.bCubemap) {
        SPDLOG_ERROR("Expected cubemap texture: {}", cubemapPath.c_str());
        return false;
    }

    if (blobView.RowPitch(0) > uploadStaging->GetStagingAllocator().GetCapacity()) {
        SPDLOG_ERROR("Cubemap block row too large for staging buffer: {}", cubemapPath.c_str());
        return false;
    }

    return true;
}

bool CubemapLoadSlot::AllocateGPUResources()
{
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
    imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.mipLevels = blobView.levelCount;
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
    viewInfo.subresourceRange.levelCount = blobView.levelCount;

    outputCubemap->imageView = Render::ImageView::CreateImageView(context, viewInfo);

    return true;
}

void CubemapLoadSlot::UploadCubemap(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadCubemap");

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    // Pre-copy barrier: UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputCubemap->image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, blobView.levelCount, 0, 6),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &preCopyBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    // Upload all mip levels for all 6 faces; faces larger than the staging buffer stream in block-row chunks
    for (uint32_t mipLevel = 0; mipLevel < blobView.levelCount; mipLevel++) {
        uint32_t mipWidth = blobView.LevelWidth(mipLevel);
        uint32_t mipHeight = blobView.LevelHeight(mipLevel);
        size_t mipSize = blobView.FaceSize(mipLevel);
        const size_t rowPitch = blobView.RowPitch(mipLevel);
        const uint32_t totalRows = std::max(1u, static_cast<uint32_t>(mipSize / rowPitch));
        const uint32_t texelRowsPerRow = (mipHeight + totalRows - 1) / totalRows;

        for (uint32_t face = 0; face < 6; face++) {
            ZoneScopedN("Upload Face");

            const uint8_t* faceData = blobView.FaceData(mipLevel, face);

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
                memcpy(stagingPtr, faceData + static_cast<size_t>(rowsDone) * rowPitch, chunkBytes);

                const uint32_t texelY = rowsDone * texelRowsPerRow;
                VkBufferImageCopy copyRegion{};
                copyRegion.bufferOffset = allocation;
                copyRegion.bufferRowLength = 0;
                copyRegion.bufferImageHeight = 0;
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.imageSubresource.mipLevel = mipLevel;
                copyRegion.imageSubresource.baseArrayLayer = face;
                copyRegion.imageSubresource.layerCount = 1;
                copyRegion.imageOffset = {0, static_cast<int32_t>(texelY), 0};
                copyRegion.imageExtent = {mipWidth, std::min(rowsFit * texelRowsPerRow, mipHeight - texelY), 1};

                vkCmdCopyBufferToImage(
                    cmd,
                    stagingBuffer.handle,
                    outputCubemap->image.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &copyRegion
                );

                rowsDone += rowsFit;
            }
        }
    }

    // Final barrier: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL with queue family transfer
    VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputCubemap->image.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, blobView.levelCount, 0, 6),
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    finalBarrier.srcQueueFamilyIndex = context->transferQueueFamily;
    finalBarrier.dstQueueFamilyIndex = context->graphicsQueueFamily;

    depInfo.pImageMemoryBarriers = &finalBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    outputCubemap->acquireBarrier = Render::VkHelpers::FromVkBarrier(finalBarrier);

    blobData = {};
    blobView = {};
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