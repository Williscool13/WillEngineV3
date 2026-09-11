//
// Created by William on 2026-04-14.
//

#ifndef WILL_ENGINE_RENDER_SCREEN_CAPTURE_H
#define WILL_ENGINE_RENDER_SCREEN_CAPTURE_H

#include <enkiTS/src/TaskScheduler.h>

#include "core/containers/inline_path.h"
#include "render/interface/render_interface.h"
#include "render/vulkan/vk_resources.h"

namespace Render
{
struct RenderScreenCapture
{
    RenderScreenCapture() = default;

    explicit RenderScreenCapture(VulkanContext* context, enki::TaskScheduler* taskScheduler)
        : context(context), taskScheduler(taskScheduler) {}

    ~RenderScreenCapture() = default;

    struct ScreenshotTask : enki::ITaskSet
    {
        RenderScreenCapture* capture{};
        uint32_t slot{0};

        void ExecuteRange(enki::TaskSetPartition, uint32_t) override;
    };

    struct ScreenshotSlot
    {
        ScreenshotTask task{};
        std::atomic_flag bInProgress{};
        AllocatedBuffer readbackBuffer{};
        uint32_t pendingFrameIndex{UINT32_MAX};
        Core::Path savePath{};
    };

    static constexpr uint32_t SCREENSHOT_SLOTS = 4;

    VulkanContext* context{};
    enki::TaskScheduler* taskScheduler{};
    AllocatedImage screenshotIntermediateImage{};
    ScreenshotSlot screenshotSlots[SCREENSHOT_SLOTS]{};
    uint32_t nextScreenshotSlot{0};
    uint32_t screenshotCaptureWidth{0};
    uint32_t screenshotCaptureHeight{0};

    std::atomic_flag bIsProbeCaptureInProgress{};
    AllocatedImage probeCaptureIntermediateImage{};
    AllocatedBuffer probeCaptureReadbackBuffer{};
    uint32_t probeCapturePendingSlot{UINT32_MAX};
    uint32_t probeCaptureSize{0};
    float probeCapturePreExposure{1.0f};
    std::atomic<bool> bProbeCaptureReady{false};

    bool CanScreenshot() const;

    /**
     * Prepares screenshot resources
     */
    void PrepareScreenshotResources(uint32_t width, uint32_t height);

    uint32_t AcquireScreenshotSlot();

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
