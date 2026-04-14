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

    bool CanScreenshot() const;

    /**
     * Prepares screenshot resources
     */
    void PrepareScreenshotResources(uint32_t width, uint32_t height);

    void StartScreenshot();

    void ResolveScreenshot(uint32_t currentFrameIndex);

    void Reset();
};
} // Render

#endif //WILL_ENGINE_RENDER_SCREEN_CAPTURE_H
