//
// Created by William on 2026-04-14.
//

#ifndef WILL_ENGINE_RENDER_SCREEN_CAPTURE_H
#define WILL_ENGINE_RENDER_SCREEN_CAPTURE_H

#include <enkiTS/src/TaskScheduler.h>

#include "core/containers/inline_path.h"
#include "core/memory/tlsf_allocator.h"
#include "render/interface/render_interface.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct RenderScreenCapture
{
    RenderScreenCapture() = default;

    explicit RenderScreenCapture(VulkanContext* context, enki::TaskScheduler* taskScheduler, Core::TlsfAllocator& renderAllocator)
        : context(context), taskScheduler(taskScheduler), renderAllocator(&renderAllocator) {}

    ~RenderScreenCapture() = default;

    struct ScreenshotTask : enki::ITaskSet
    {
        RenderScreenCapture* capture{};

        void ExecuteRange(enki::TaskSetPartition, uint32_t) override;
    };


    VulkanContext* context{};
    enki::TaskScheduler* taskScheduler{};
    Core::TlsfAllocator* renderAllocator{};
    ScreenshotTask task{};
    std::atomic_flag bIsScreenshotInProgress{};
    AllocatedImage screenshotIntermediateImage{};
    AllocatedBuffer screenshotReadbackBuffer{};
    uint32_t screenshotPendingSlot{UINT32_MAX};
    uint32_t screenshotCaptureWidth{0};
    uint32_t screenshotCaptureHeight{0};
    Core::Path screenshotSavePath{};

    std::atomic_flag bIsProbeCaptureInProgress{};
    AllocatedImage probeCaptureIntermediateImage{};
    AllocatedBuffer probeCaptureReadbackBuffer{};
    uint32_t probeCapturePendingSlot{UINT32_MAX};
    uint32_t probeCaptureSize{0};
    std::atomic<bool> bProbeCaptureReady{false};

    bool CanScreenshot() const;

    /**
     * Prepares screenshot resources
     */
    void PrepareScreenshotResources(uint32_t width, uint32_t height);

    void StartScreenshot();

    void ResolveScreenshot(uint32_t currentFrameIndex);

    bool CanProbeCapture() const;

    void PrepareProbeCaptureResources(uint32_t size);

    void StartProbeCapture();

    void ResolveProbeCapture(uint32_t currentFrameIndex);

    /** @returns true once a probe-face capture has landed in the mapped readback buffer and has not yet been released. */
    bool IsProbeCaptureReady() const;

    const uint16_t* GetProbeCapturePixels() const;

    uint32_t GetProbeCaptureCaptureSize() const;

    /** Clears the ready + in-flight state, allowing a new probe-face capture to be requested. */
    void ReleaseProbeCapture();

    void Reset();
};
} // Render

#endif //WILL_ENGINE_RENDER_SCREEN_CAPTURE_H
