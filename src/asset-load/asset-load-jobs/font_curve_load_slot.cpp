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
}

void FontCurveLoadSlot::LoadTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    if (!loadSlot->LoadCurvesFromDisk() || !loadSlot->AllocateResidency()) {
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

    VK_CHECK(vkEndCommandBuffer(cmd));
    std::binary_semaphore done(0);
    loadSlot->_requestDispatchCallback(cmd, fence, &done);
    done.acquire();

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
    if (header.slugUncompressedSize == 0) { return true; }

    const Core::Path& fontPath = outputFont->source;
    Platform::ScopedFileMapping map(fontPath);
    if (!map.data || header.slugDataOffset + header.slugDataSize > map.size) {
        LOG_ERROR(Asset, "Failed to read .wsfont slug data: {}", fontPath.c_str());
        return false;
    }

    blobData = Core::HeapArray<uint8_t>(&memoryManager->AssetsScratch(), Core::AllocTag::AssetManager, header.slugUncompressedSize);
    Engine::Decompress(header.slugCompressionType, map.data + header.slugDataOffset, header.slugDataSize, blobData.Data(), header.slugUncompressedSize);
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
