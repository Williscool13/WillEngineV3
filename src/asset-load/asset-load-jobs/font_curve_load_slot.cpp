//
// Created by William on 2026-08-14.
//

#include "font_curve_load_slot.h"

#include <semaphore>

#include "asset-load/asset_load_config.h"
#include "core/memory/memory_manager.h"
#include "engine/compression/compression.h"
#include "engine/logging/engine_log.h"
#include "engine/resources/font/font.h"
#include "platform/file_utils.h"
#include "render/resource_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
FontCurveLoadSlot::FontCurveLoadSlot() = default;

FontCurveLoadSlot::~FontCurveLoadSlot()
{
    transferSubmit.Destroy(context);
}

void FontCurveLoadSlot::Initialize(
    enki::TaskScheduler* _scheduler,
    Render::VulkanContext* _context,
    Render::ResourceManager* _resourceManager,
    Core::MemoryManager* _memoryManager,
    Core::InlineFunction<void(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)> dispatchCallback,
    Core::InlineFunction<void(bool success, FontCurveSlotHandle slotHandle)> notifyCallback)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;
    memoryManager = _memoryManager;
    _requestDispatchCallback = std::move(dispatchCallback);
    _notifyCallback = std::move(notifyCallback);

    transferSubmit.Initialize(context, context->transferQueueFamily);
}

void FontCurveLoadSlot::Launch(FontCurveSlotHandle _slotHandle, UploadStaging* _uploadStaging, Engine::Font* _outputFont)
{
    slotHandle = _slotHandle;
    uploadStaging = _uploadStaging;
    outputFont = _outputFont;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void FontCurveLoadSlot::Clear()
{
    slotHandle = {};
    outputFont = nullptr;
    uploadStaging = nullptr;
    blobData = {};
    atlasData = {};
}

void FontCurveLoadSlot::LoadTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadCurvesFromDisk() || !loadSlot->AllocateResidency() || !loadSlot->AllocateAtlasResources()) {
        loadSlot->_notifyCallback(false, loadSlot->slotHandle);
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

    loadSlot->UploadCurves(cmd, submitAndWait);
    loadSlot->UploadAtlas(cmd, submitAndWait);

    VK_CHECK(vkEndCommandBuffer(cmd));
    std::binary_semaphore done(0);
    loadSlot->_requestDispatchCallback(cmd, fence, &done);
    done.acquire();
    VK_CHECK(vkWaitForFences(loadSlot->context->device, 1, &fence, VK_TRUE, UINT64_MAX));

    loadSlot->PostUploadSetup();

    loadSlot->transferSubmit.Reset(loadSlot->context);

    loadSlot->_notifyCallback(true, loadSlot->slotHandle);
}

bool FontCurveLoadSlot::LoadCurvesFromDisk()
{
    ZoneScopedN("LoadFontCurvesFromDisk");

    if (!outputFont) {
        LOG_ERROR(Asset, "Output font is null");
        return false;
    }

    const Engine::WFontHeader& header = outputFont->header;
    if (header.slugUncompressedSize == 0 && header.sdfUncompressedSize == 0) { return true; }

    const Core::Path& fontPath = outputFont->source;
    Platform::ScopedFileMapping map(fontPath);
    if (!map.data || header.slugDataOffset + header.slugDataSize > map.size || header.sdfDataOffset + header.sdfDataSize > map.size) {
        LOG_ERROR(Asset, "Failed to read .wsfont slug data: {}", fontPath.c_str());
        return false;
    }

    if (header.slugUncompressedSize > 0) {
        blobData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager, header.slugUncompressedSize);
        Engine::Decompress(header.slugCompressionType, map.data + header.slugDataOffset, header.slugDataSize, blobData.Data(), header.slugUncompressedSize);
    }
    if (header.sdfUncompressedSize > 0) {
        atlasData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager, header.sdfUncompressedSize);
        Engine::Decompress(header.slugCompressionType, map.data + header.sdfDataOffset, header.sdfDataSize, atlasData.Data(), header.sdfUncompressedSize);
    }
    return true;
}

bool FontCurveLoadSlot::AllocateResidency() const
{
    if (blobData.Size() == 0) { return true; }

    std::lock_guard lock(resourceManager->fontCurveAllocatorMutex);
    const OffsetAllocator::Allocation alloc = resourceManager->fontCurveAllocator.allocate(static_cast<uint32_t>(blobData.Size() + 8));
    if (alloc.offset == OffsetAllocator::Allocation::NO_SPACE) {
        LOG_ERROR(Asset, "Font curve megabuffer exhausted loading {}", outputFont->name.c_str());
        return false;
    }
    outputFont->curveAllocation = alloc;
    outputFont->curveByteOffset = (alloc.offset + 7u) & ~7u;
    return true;
}

bool FontCurveLoadSlot::AllocateAtlasResources() const
{
    if (atlasData.Size() == 0) { return true; }

    const Engine::WFontHeader& header = outputFont->header;
    const uint32_t atlasW = header.sdfCols * header.sdfCellPx;
    const uint32_t atlasH = header.sdfRows * header.sdfCellPx;
    if (atlasW == 0 || atlasH == 0 || static_cast<uint64_t>(atlasW) * atlasH != header.sdfUncompressedSize) {
        LOG_ERROR(Asset, "Font SDF atlas dimensions mismatch: {}", outputFont->name.c_str());
        return false;
    }

    VkImageCreateInfo imageCreateInfo = Render::VkHelpers::ImageCreateInfo(
        VK_FORMAT_R8_UNORM,
        VkExtent3D{atlasW, atlasH, 1},
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    outputFont->atlasImage = Render::AllocatedImage::CreateAllocatedImage(context, imageCreateInfo);

    VkImageViewCreateInfo viewInfo = Render::VkHelpers::ImageViewCreateInfo(
        outputFont->atlasImage.handle,
        outputFont->atlasImage.format,
        VK_IMAGE_ASPECT_COLOR_BIT
    );
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.subresourceRange.levelCount = 1;

    outputFont->atlasImageView = Render::ImageView::CreateImageView(context, viewInfo);
    return true;
}

void FontCurveLoadSlot::UploadAtlas(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadFontSdfAtlas");

    if (atlasData.Size() == 0) { return; }

    const Engine::WFontHeader& header = outputFont->header;
    const uint32_t atlasW = header.sdfCols * header.sdfCellPx;
    const uint32_t atlasH = header.sdfRows * header.sdfCellPx;

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    VkImageMemoryBarrier2 preCopyBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputFont->atlasImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );
    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &preCopyBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    size_t allocation = stagingAllocator.Allocate(atlasData.Size(), 16);
    if (allocation == SIZE_MAX) {
        submitAndWait(true);
        stagingAllocator.Reset();
        allocation = stagingAllocator.Allocate(atlasData.Size(), 16);
        assert(allocation != SIZE_MAX && "Font SDF atlas larger than staging buffer");
    }
    char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
    memcpy(stagingPtr, atlasData.Data(), atlasData.Size());

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = allocation;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = {atlasW, atlasH, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer.handle, outputFont->atlasImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    VkImageMemoryBarrier2 finalBarrier = Render::VkHelpers::ImageMemoryBarrier(
        outputFont->atlasImage.handle,
        Render::VkHelpers::SubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1),
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    finalBarrier.srcQueueFamilyIndex = context->transferQueueFamily;
    finalBarrier.dstQueueFamilyIndex = context->graphicsQueueFamily;

    depInfo.pImageMemoryBarriers = &finalBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    outputFont->atlasAcquireBarrier = Render::VkHelpers::FromVkBarrier(finalBarrier);

    atlasData = {};
}

void FontCurveLoadSlot::PostUploadSetup() const
{
    if (outputFont->atlasImageView.handle == VK_NULL_HANDLE) { return; }

    const bool updateRes = resourceManager->bindlessSamplerTextureDescriptorBuffer.UpdateTexture(
        outputFont->atlasBindlessHandle, {
            .imageView = outputFont->atlasImageView.handle,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });
    if (!updateRes) {
        LOG_ERROR(Asset, "Failed to update font SDF atlas bindless descriptor: {}", outputFont->name.c_str());
    }
}

void FontCurveLoadSlot::UploadCurves(VkCommandBuffer cmd, const Core::InlineFunction<void(bool)>& submitAndWait)
{
    ZoneScopedN("UploadFontCurves");

    if (blobData.Size() == 0) { return; }

    Core::LinearAllocator& stagingAllocator = uploadStaging->GetStagingAllocator();
    Render::AllocatedBuffer& stagingBuffer = uploadStaging->GetStagingBuffer();

    const size_t totalBytes = blobData.Size();
    size_t bytesDone = 0;
    while (bytesDone < totalBytes) {
        size_t chunk = std::min(totalBytes - bytesDone, stagingAllocator.GetRemaining());
        if (chunk == 0) {
            submitAndWait(true);
            stagingAllocator.Reset();
            continue;
        }
        const size_t allocation = stagingAllocator.Allocate(chunk, 16);
        if (allocation == SIZE_MAX) {
            submitAndWait(true);
            stagingAllocator.Reset();
            continue;
        }

        char* stagingPtr = static_cast<char*>(stagingBuffer.allocationInfo.pMappedData) + allocation;
        memcpy(stagingPtr, blobData.Data() + bytesDone, chunk);

        VkBufferCopy copyRegion{
            .srcOffset = allocation,
            .dstOffset = outputFont->curveByteOffset + bytesDone,
            .size = chunk,
        };
        vkCmdCopyBuffer(cmd, stagingBuffer.handle, resourceManager->megaFontCurveBuffer.handle, 1, &copyRegion);

        bytesDone += chunk;
    }

    VkBufferMemoryBarrier2 releaseBarrier{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
        .srcQueueFamilyIndex = context->transferQueueFamily,
        .dstQueueFamilyIndex = context->graphicsQueueFamily,
        .buffer = resourceManager->megaFontCurveBuffer.handle,
        .offset = outputFont->curveByteOffset,
        .size = totalBytes,
    };
    if (context->bMaintenance9Enabled) {
        releaseBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        releaseBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    }

    VkDependencyInfo depInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers = &releaseBarrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);

    outputFont->bufferAcquireOps.PushBack(Render::VkHelpers::FromVkBarrier(releaseBarrier));

    blobData = {};
}
} // AssetLoad
