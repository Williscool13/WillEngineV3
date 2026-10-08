//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_RENDER_THREAD_H
#define WILLENGINEV3_RENDER_THREAD_H

#include <atomic>
#include <chrono>

#include "core/containers/vector.h"
#include "render/frame_resources.h"
#include "asset-load/async_asset_load_manager.h"
#include "core/containers/array.h"
#include "render/interface/render_interface.h"
#include "render/renderer_statistics.h"
#include "render/render-graph/render_graph_resources.h"
#include "render/vulkan/vk_pipeline_stats.h"
#include "render/vulkan/vk_resources.h"
#include "render/vulkan/vk_synchronization.h"
#include "render/systems/render_screen_capture.h"
#include "render/types/render_types.h"
#include "render/post-processing/post_processing.h"
#include "render/frame_outputs.h"
#include "render/shaders/ddgi_interop.h"

#include <imgui.h>
#include <imgui_threaded_rendering.h>

#include "render/passes/ddgi_passes.h"
#include "core/types/extent.h"

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
struct FrameContext;

/**
 * The main render thread
 */
class RenderThread
{
    enum RenderResponseCode
    {
        SUCCESS,
        RENDER_REQUESTED_RECREATE,
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

    RenderResponseCode RecordFrame(uint32_t frameIndex, VkCommandBuffer cmd, VkCommandBuffer asyncCmd, Core::FrameBuffer& frameBuffer, ImDrawDataSnapshot& imguiSnapshot);

    void RecordPresent(VkCommandBuffer cmd, uint32_t swapchainImageIndex, const Core::FrameBuffer& frameBuffer, ImDrawDataSnapshot& imguiSnapshot);

    void ProcessAcquisitions(VkCommandBuffer cmd, Core::Span<Core::ImageAcquireOperation> imageAcquireOperations);

public:
    VulkanContext* GetVulkanContext() const { return context; }
    ResourceManager* GetResourceManager() const { return resourceManager; }
    PipelineManager* GetPipelineManager() const { return pipelineManager; }
    RendererStatistics GetRendererStatistics() { return statisticsManager.GetPublished(); }
    RendererStatisticsManager* GetStatisticsManager() { return &statisticsManager; }
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
    float GetProbeCapturePreExposure() const { return screenCapture->probeCapturePreExposure; }
    void ReleaseProbeCapture() { screenCapture->ReleaseProbeCapture(); }

private:
    void UploadFrameUniforms(const Core::ViewFamily& viewFamily, Core::Extent2D renderExtent, float renderDeltaTime, SceneResources& scene) const;

    void UploadModelUniforms(Core::ViewFamily& viewFamily, const SceneBufferSizes& bufferSizes, SceneResources& scene) const;

    void UploadTextUniforms(Core::ViewFamily& viewFamily, const SceneBufferSizes& bufferSizes, SceneResources& scene) const;

    void UploadUIUniforms(const Core::ViewFamily& viewFamily, const SceneBufferSizes& bufferSizes, SceneResources& scene) const;

    void UploadSpriteUniforms(const Core::ViewFamily& viewFamily, SceneResources& scene) const;

    void SetupDebugRender(RenderGraph& graph, const Core::ViewFamily& viewFamily, const SceneResources& scene, Core::Extent2D renderExtent, RDGTexture depthTarget, RDGTexture targetImage, FrameResourceLimits& limits) const;

    // RecordFrame phases, in record_frame.cpp. Pass declaration order is the order they are called.
    void ApplyRenderReset(Core::RenderReset reset);

    FrameContext BeginFrame(uint32_t frameIndex, Core::FrameBuffer& frameBuffer);

    void RecordFrameSetup(FrameContext& ctx, VkCommandBuffer cmd, VkCommandBuffer asyncCmd);

    void RecordSceneServices(FrameContext& ctx);

    void RecordDDGI(FrameContext& ctx, const DDGICascades& ddgiCascades);

    /** Every lighting path ends with composited HDR in ctx.targets.colorOutput; demodulated buffers stay inside the ReSTIR path. */
    void RecordLighting(FrameContext& ctx);

    void RecordGroundTruth(FrameContext& ctx);

    void RecordLightingAnalytic(FrameContext& ctx);

    void RecordLightingReSTIR(FrameContext& ctx);

    void RecordSunShadows(FrameContext& ctx);

    void RecordPostLighting(FrameContext& ctx);

    void RecordPresentation(FrameContext& ctx);

    void RecordProbeCapture(FrameContext& ctx);

#if WILL_EDITOR
    void RecordDiagnostics(FrameContext& ctx);

    void RecordDebugVisualize(FrameContext& ctx);
#endif

    void RecordFrameExport(FrameContext& ctx);

    void RecordScreenshot(FrameContext& ctx);

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
    int32_t lastFogDebugMode{0};
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

    Core::Vector<VkImageMemoryBarrier2> tempImageBarriers;

    uint32_t currentFrameInFlight{0};
    uint64_t frameNumber{0};
    float preExposure{1.0f};
    float prevPreExposure{1.0f};
    float framerateScale{1.0f};
    std::chrono::steady_clock::time_point lastWallFrameTime{};
    float smoothedWallFrameMs{0.0f};
    float smoothedGpuSpanMs{0.0f};
    uint32_t rtGroundTruthDIAccumCount{0};
    // Extent the kept local shadow atlas was last declared with
    glm::uvec2 localShadowAtlasExtent{0};
    uint32_t rtGroundTruthGIAccumCount{0};
    uint32_t rtGroundTruthFullAccumCount{0};
    uint32_t previousRestirCheckerboardField{0};
    bool previousRestirFullRateResolve{false};
    DDGICascades ddgiPreviousCascades{};
    FrameResourceLimits frameResourceLimits{};
    bool bEngineRequestsRecreate{false};
    bool bRenderRequestsRecreate{false};
    RDGTexture presentSourceTexture{};

#if WILL_EDITOR
    struct DebugCursorReadback
    {
        RDGTexture litTexture{};
        uint32_t pixel[2]{};
    };
    DebugCursorReadback debugCursorReadback{};
#endif

private:
    RenderScreenCapture* screenCapture{};
};
} // Render

#endif //WILLENGINEV3_RENDER_THREAD_H
