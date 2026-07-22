//
// Created by William on 2026-04-14.
//

#include "render_screen_capture.h"

#include <stb/stb_image_write.h>

#include "engine/logging/engine_log.h"

namespace Render
{
void RenderScreenCapture::ScreenshotTask::ExecuteRange(enki::TaskSetPartition, uint32_t)
{
    const uint32_t width   = capture->screenshotCaptureWidth;
    const uint32_t height  = capture->screenshotCaptureHeight;
    const size_t rowBytes  = static_cast<size_t>(width) * 4;
    const uint8_t* src     = static_cast<const uint8_t*>(capture->screenshotReadbackBuffer.allocationInfo.pMappedData);

    // LUT: linear uint8 -> sRGB uint8
    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) {
        float linear = i / 255.0f;
        float srgb   = linear <= 0.0031308f
                         ? 12.92f * linear
                         : 1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
        lut[i] = static_cast<uint8_t>(srgb * 255.0f + 0.5f);
    }

    const size_t bufferSize = rowBytes * height;
    uint8_t* encoded = static_cast<uint8_t*>(capture->renderAllocator->Alloc(bufferSize, Core::AllocTag::Render));
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* srcRow = src + (height - 1 - row) * rowBytes;  // y-flip
        uint8_t*       dstRow = encoded + row * rowBytes;
        for (uint32_t x = 0; x < width; ++x) {
            dstRow[x * 4 + 0] = lut[srcRow[x * 4 + 0]];
            dstRow[x * 4 + 1] = lut[srcRow[x * 4 + 1]];
            dstRow[x * 4 + 2] = lut[srcRow[x * 4 + 2]];
            dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
        }
    }

    stbi_write_png(
        capture->screenshotSavePath.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        4,
        encoded,
        static_cast<int>(rowBytes)
    );
    capture->renderAllocator->Free(encoded);

    LOG_INFO(Renderer, "Screenshot saved: {}", capture->screenshotSavePath.c_str());
    capture->bIsScreenshotInProgress.clear();
}

bool RenderScreenCapture::CanScreenshot() const
{
    auto bIsSet = bIsScreenshotInProgress.test();
    return !bIsSet;
}

void RenderScreenCapture::PrepareScreenshotResources(uint32_t width, uint32_t height)
{
    if (screenshotIntermediateImage.handle != VK_NULL_HANDLE && screenshotIntermediateImage.extent.width == width && screenshotIntermediateImage.extent.height == height) {
        return;
    }

    screenshotIntermediateImage = AllocatedImage{};
    screenshotReadbackBuffer = AllocatedBuffer{};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    screenshotIntermediateImage = AllocatedImage::CreateAllocatedImage(context, imageInfo);
    screenshotIntermediateImage.SetDebugName("screenshot_intermediate");

    const size_t bufferSize = static_cast<size_t>(width) * height * 4;
    screenshotReadbackBuffer = AllocatedBuffer::CreateAllocatedReceivingBuffer(context, bufferSize);
    screenshotReadbackBuffer.SetDebugName("screenshot_readback");
    screenshotCaptureWidth = width;
    screenshotCaptureHeight = height;
}

void RenderScreenCapture::StartScreenshot()
{
    auto bWasInProgress = bIsScreenshotInProgress.test_and_set();
    assert(!bWasInProgress);
}

void RenderScreenCapture::ResolveScreenshot(uint32_t currentFrameIndex)
{
    if (screenshotPendingSlot == currentFrameIndex) {
        screenshotPendingSlot = UINT32_MAX;
        task.capture = this;
        taskScheduler->AddTaskSetToPipe(&task);
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
    bIsScreenshotInProgress.clear();
    screenshotIntermediateImage = {};
    screenshotReadbackBuffer = {};
    screenshotCaptureWidth = {};
    screenshotCaptureHeight = {};
    screenshotSavePath = {};
    bIsProbeCaptureInProgress.clear();
    probeCaptureIntermediateImage = {};
    probeCaptureReadbackBuffer = {};
    probeCapturePendingSlot = UINT32_MAX;
    probeCaptureSize = {};
    bProbeCaptureReady.store(false, std::memory_order_relaxed);
}
} // Render
