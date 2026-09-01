//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_RENDER_THREAD_H
#define WILLENGINEV3_RENDER_THREAD_H

#include <atomic>
#include <chrono>

#include "core/containers/vector.h"
#include "frame_resources.h"
#include "asset-load/async_asset_load_manager.h"
#include "core/containers/array.h"
#include "interface/render_interface.h"
#include "render/renderer_statistics.h"
#include "render/render-graph/render_graph_resources.h"
#include "render/vulkan/vk_pipeline_stats.h"
#include "render/vulkan/vk_resources.h"
#include "render/vulkan/vk_synchronization.h"
#include "systems/render_screen_capture.h"
#include "types/render_types.h"
#include "post-processing/post_processing.h"
#include "render/shaders/ddgi_interop.h"

#include <imgui.h>
#include <imgui_threaded_rendering.h>

#include "passes/ddgi_passes.h"

namespace AssetLoad
{
class GpuAssetUploadThread;
}

namespace Render
{
class GPUDispatcher;
class NrdDenoiser;
class PipelineManager;
class RenderGraph;
}

namespace Core
{
struct FrameSync;
class MemoryManager;
}

namespace enki
{
class LambdaPinnedTask;
class TaskScheduler;
}

struct SDL_Window;

namespace Engine
{
class WillEngine;
}

namespace Render
{
struct ResourceManager;
struct RenderExtents;
struct Swapchain;
struct VulkanContext;
struct ImguiWrapper;
}

namespace Render
{
/**
 * The main render thread
 */
class RenderThread
{
    enum RenderResponseCode
    {
        SUCCESS,
        RENDER_REQUESTED_RECREATE,
        SWAPCHAIN_OUTDATED
    };

    struct RenderResponse
    {
        RenderResponseCode code;
        uint32_t swapchainIndex; // meaningless if code is not success
    };

public:
    RenderThread();

    RenderThread(Core::MemoryManager& memoryManager, Core::FrameSync* engineRenderSynchronization, enki::TaskScheduler* scheduler, SDL_Window* window, uint32_t width, uint32_t height);

    ~RenderThread();

    void InitializePipelineManager(AssetLoad::AsyncAssetLoadManager* _asyncAssetLoadManager, GPUDispatcher* _gpuDispatcher);

    void Start();

    void RequestShutdown();

    /** Set by the render thread when it detects an unrecoverable frame fault (e.g. undeclared RDG resource access)*/
    [[nodiscard]] bool IsShutdownRequestedByRender() const { return bRenderRequestsShutdown.load(std::memory_order_relaxed); }

    void Join();

    void ThreadMain();

    void RenderFrame(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer, ImDrawDataSnapshot& imguiSnapshot);

    RenderResponse RecordFrame(uint32_t frameIndex, VkCommandBuffer cmd, VkCommandBuffer asyncCmd, VkSemaphore swapchainSemaphore, Core::FrameBuffer& frameBuffer, ImDrawDataSnapshot& imguiSnapshot);

    void ProcessAcquisitions(VkCommandBuffer cmd, Core::Span<Core::BufferAcquireOperation> bufferAcquireOperations, Core::Span<Core::ImageAcquireOperation> imageAcquireOperations);

public:
    VulkanContext* GetVulkanContext() const { return context; }
    ResourceManager* GetResourceManager() const { return resourceManager; }
    PipelineManager* GetPipelineManager() const { return pipelineManager; }
    RendererStatistics GetRendererStatistics() { return statisticsManager.GetPublished(); }
    void RequestVRAMReport() { bVRAMReportShouldWrite.store(true, std::memory_order_relaxed); }

    /** @returns the latest VRAM snapshot if one is ready, otherwise an empty report. */
    Render::VRAMReport GetVRAMReport()
    {
        if (!bVRAMReportShouldRead.load(std::memory_order_acquire)) {
            return {};
        }
        Render::VRAMReport snapshot = vramReport;
        bVRAMReportShouldRead.store(false, std::memory_order_relaxed);
        return snapshot;
    }

    bool IsScreenshotInFlight() const { return !screenCapture->CanScreenshot(); }
    bool IsProbeCaptureReady() const { return screenCapture->IsProbeCaptureReady(); }
    const uint16_t* GetProbeCapturePixels() const { return screenCapture->GetProbeCapturePixels(); }
    uint32_t GetProbeCaptureSize() const { return screenCapture->GetProbeCaptureCaptureSize(); }
    void ReleaseProbeCapture() { screenCapture->ReleaseProbeCapture(); }

private:
    void UploadFrameUniforms(const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, float renderDeltaTime) const;

    void UploadModelUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const;

    void UploadTextUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const;

    void UploadUIUniforms(const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const;

    void UploadSpriteUniforms(const Core::ViewFamily& viewFamily) const;

    /*void SetupPortalComposite(RenderGraph& graph, const Core::ViewFamily& renderViewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets,
                              const MainRenderTargets& portalTargets) const;*/

    void SetupDebugRender(RenderGraph& graph, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, StringID depthTarget, StringID targetImage, FrameResourceLimits& limits) const;

#if WILL_EDITOR
    void RegisterDebugReadbacks();
#endif

private:
    // Non-owning
    Core::MemoryManager* memoryManager{};
    SDL_Window* window{};
    Core::FrameSync* engineRenderSynchronization{};
    enki::TaskScheduler* scheduler{};
    GPUDispatcher* gpuDispatcher{};

    // Threading
    std::atomic<bool> bShouldExit{false};
    std::atomic<bool> bRenderRequestsShutdown{false};
    std::jthread thisThread;

    // Owning
    VulkanContext* context{};
    Swapchain* swapchain{};
    ImguiWrapper* imgui{};
    ResourceManager* resourceManager{};
    RenderExtents* renderExtents{};
    float lastResolutionScale{1.0f};
    PipelineManager* pipelineManager{};
    NrdDenoiser* nrdDenoiser{};

    Core::VirtualArena renderArena{};
    RenderGraph* renderGraph{};

    Core::Array<RenderSynchronization, Core::FRAME_BUFFER_COUNT> frameSynchronization;
    VkSemaphore asyncComputeTimelineSemaphore{};
    uint64_t asyncComputeTimelineValue{0};

    PipelineStatsQueryPool pipelineStatsQuery{};
    RendererStatisticsManager statisticsManager{};
    std::atomic<bool> bVRAMReportShouldWrite{false};
    std::atomic<bool> bVRAMReportShouldRead{false};
    Render::VRAMReport vramReport{};

    Core::Vector<VkBufferMemoryBarrier2> tempBufferBarriers;
    Core::Vector<VkImageMemoryBarrier2> tempImageBarriers;

    uint32_t currentFrameInFlight{0};
    uint64_t frameNumber{0};
    std::chrono::steady_clock::time_point lastWallFrameTime{};
    float smoothedWallFrameMs{0.0f};
    float smoothedGpuSpanMs{0.0f};
    uint32_t rtGroundTruthDIAccumCount{0};
    uint32_t rtGroundTruthGIAccumCount{0};
    uint32_t rtGroundTruthFullAccumCount{0};
    uint32_t previousRestirCheckerboardField{0};
    bool previousRestirFullRateResolve{false};
    DDGICascades ddgiPreviousCascades{};
    FrameResourceLimits frameResourceLimits{};
    bool bEngineRequestsRecreate{false};
    bool bRenderRequestsRecreate{false};

#if WILL_EDITOR
    struct DebugCursorReadback
    {
        StringID litTexture{};
        uint32_t pixel[2]{};
    };
    DebugCursorReadback debugCursorReadback{};
#endif

private:
    RenderScreenCapture* screenCapture{};
};
} // Render

#endif //WILLENGINEV3_RENDER_THREAD_H
