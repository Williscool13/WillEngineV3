//
// Created by William on 2026-04-14.
//

#include "render_screen_capture.h"

#include <thread>

#include "engine/logging/engine_log.h"
#include "utils/image/png_write.h"

namespace Render
{
void RenderScreenCapture::ScreenshotTask::ExecuteRange(enki::TaskSetPartition, uint32_t)
{
    ScreenshotSlot& s = capture->screenshotSlots[slot];
    const uint8_t* pixels = static_cast<const uint8_t*>(s.readbackBuffer.allocationInfo.pMappedData);
    if (Utils::WritePngRgba8(s.savePath.c_str(), capture->screenshotCaptureWidth, capture->screenshotCaptureHeight, pixels, true)) {
        LOG_INFO(Renderer, "Screenshot saved: {}", s.savePath.c_str());
    }
    else {
        LOG_WARN(Renderer, "Screenshot open failed: {}", s.savePath.c_str());
    }
    s.bInProgress.clear();
}

bool RenderScreenCapture::CanScreenshot() const
{
    for (const ScreenshotSlot& s : screenshotSlots) {
        if (s.bInProgress.test()) { return false; }
    }
    return true;
}

void RenderScreenCapture::PrepareScreenshotResources(uint32_t width, uint32_t height)
{
    if (screenshotIntermediateImage.handle != VK_NULL_HANDLE && screenshotIntermediateImage.extent.width == width && screenshotIntermediateImage.extent.height == height) {
        return;
    }

    for (ScreenshotSlot& s : screenshotSlots) {
        while (s.bInProgress.test()) { std::this_thread::yield(); }
        s.readbackBuffer = AllocatedBuffer{};
    }
    screenshotIntermediateImage = AllocatedImage{};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    screenshotIntermediateImage = AllocatedImage::CreateAllocatedImage(context, imageInfo);
    screenshotIntermediateImage.SetDebugName("screenshot_intermediate");
    screenshotCaptureWidth = width;
    screenshotCaptureHeight = height;
}

uint32_t RenderScreenCapture::AcquireScreenshotSlot()
{
    const uint32_t slot = nextScreenshotSlot;
    nextScreenshotSlot = (nextScreenshotSlot + 1) % SCREENSHOT_SLOTS;
    ScreenshotSlot& s = screenshotSlots[slot];
    while (s.bInProgress.test_and_set()) { std::this_thread::yield(); }

    const size_t bufferSize = static_cast<size_t>(screenshotCaptureWidth) * screenshotCaptureHeight * 4;
    if (s.readbackBuffer.handle == VK_NULL_HANDLE || s.readbackBuffer.size != bufferSize) {
        s.readbackBuffer = AllocatedBuffer::CreateAllocatedReceivingBuffer(context, bufferSize);
        s.readbackBuffer.SetDebugName("screenshot_readback");
    }
    return slot;
}

void RenderScreenCapture::ResolveScreenshot(uint32_t currentFrameIndex)
{
    for (uint32_t i = 0; i < SCREENSHOT_SLOTS; ++i) {
        ScreenshotSlot& s = screenshotSlots[i];
        if (s.pendingFrameIndex != currentFrameIndex) { continue; }
        s.pendingFrameIndex = UINT32_MAX;
        s.task.capture = this;
        s.task.slot = i;
        taskScheduler->AddTaskSetToPipe(&s.task);
    }
}

bool RenderScreenCapture::CanProbeCapture() const
{
    return !bIsProbeCaptureInProgress.test();
}

void RenderScreenCapture::PrepareProbeCaptureResources(uint32_t size)
{
    if (probeCaptureIntermediateImage.handle != VK_NULL_HANDLE && probeCaptureIntermediateImage.extent.width == size && probeCaptureIntermediateImage.extent.height == size) {
        return;
    }

    probeCaptureIntermediateImage = AllocatedImage{};
    probeCaptureReadbackBuffer = AllocatedBuffer{};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {size, size, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    probeCaptureIntermediateImage = AllocatedImage::CreateAllocatedImage(context, imageInfo);
    probeCaptureIntermediateImage.SetDebugName("probe_capture_intermediate");

    const size_t bufferSize = static_cast<size_t>(size) * size * 8;
    probeCaptureReadbackBuffer = AllocatedBuffer::CreateAllocatedReceivingBuffer(context, bufferSize);
    probeCaptureReadbackBuffer.SetDebugName("probe_capture_readback");
    probeCaptureSize = size;
}

void RenderScreenCapture::StartProbeCapture()
{
    auto bWasInProgress = bIsProbeCaptureInProgress.test_and_set();
    assert(!bWasInProgress);
}

void RenderScreenCapture::ResolveProbeCapture(uint32_t currentFrameIndex)
{
    if (probeCapturePendingSlot == currentFrameIndex) {
        probeCapturePendingSlot = UINT32_MAX;
        bProbeCaptureReady.store(true, std::memory_order_release);
    }
}

bool RenderScreenCapture::IsProbeCaptureReady() const
{
    return bProbeCaptureReady.load(std::memory_order_acquire);
}

const uint16_t* RenderScreenCapture::GetProbeCapturePixels() const
{
    return static_cast<const uint16_t*>(probeCaptureReadbackBuffer.allocationInfo.pMappedData);
}

uint32_t RenderScreenCapture::GetProbeCaptureCaptureSize() const
{
    return probeCaptureSize;
}

void RenderScreenCapture::ReleaseProbeCapture()
{
    bProbeCaptureReady.store(false, std::memory_order_release);
    bIsProbeCaptureInProgress.clear();
}

void RenderScreenCapture::Reset()
{
    taskScheduler = {};
    for (ScreenshotSlot& s : screenshotSlots) {
        s.bInProgress.clear();
        s.readbackBuffer = {};
        s.pendingFrameIndex = UINT32_MAX;
        s.savePath = {};
    }
    nextScreenshotSlot = 0;
    screenshotIntermediateImage = {};
    screenshotCaptureWidth = {};
    screenshotCaptureHeight = {};
    bIsProbeCaptureInProgress.clear();
    probeCaptureIntermediateImage = {};
    probeCaptureReadbackBuffer = {};
    probeCapturePendingSlot = UINT32_MAX;
    probeCaptureSize = {};
    bProbeCaptureReady.store(false, std::memory_order_relaxed);
}
} // Render
