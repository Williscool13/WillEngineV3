//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_RENDER_THREAD_H
#define WILLENGINEV3_RENDER_THREAD_H

#include <array>
#include <atomic>
#include <memory>

#include "frame_resources.h"
#include "asset-load/async_asset_load_manager.h"
#include "core/include/render_interface.h"
#include "render/vulkan/vk_synchronization.h"
#include "types/render_types.h"

namespace AssetLoad
{
class GpuAssetUploadThread;
}

namespace Render
{
class PipelineManager;
class RenderGraph;
}

namespace Core
{
struct FrameSync;
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

    RenderThread(Core::FrameSync* engineRenderSynchronization, enki::TaskScheduler* scheduler, SDL_Window* window, uint32_t width, uint32_t height);

    ~RenderThread();

    void InitializePipelineManager(AssetLoad::AsyncAssetLoadManager* _asyncAssetLoadManager);

    void Start();

    void RequestShutdown();

    void Join();

    void ThreadMain();

    void RenderFrame(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer);

    RenderResponse RecordFrame(uint32_t frameIndex, VkCommandBuffer cmd, VkSemaphore swapchainSemaphore, Core::FrameBuffer& frameBuffer);

    void ProcessAcquisitions(VkCommandBuffer cmd, const std::vector<Core::BufferAcquireOperation>& bufferAcquireOperations, const std::vector<Core::ImageAcquireOperation>& imageAcquireOperations);

public:
    VulkanContext* GetVulkanContext() const { return context.get(); }
    ResourceManager* GetResourceManager() const { return resourceManager.get(); }
    PipelineManager* GetPipelineManager() const { return pipelineManager.get(); }

private:
    void CreatePipelines();

    void PrepareRenderFamilyProperties(Core::ViewFamily& viewFamily, ReadbackStruct* readbackData, RenderFamilyProperties& renderFamilyProperties, PipelineManager* _pipelineManager, FrameResourceLimits& _limits);

    void UploadFrameUniforms(const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, float renderDeltaTime) const;

    void UploadModelUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const;

    void SetupCascadedShadows(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties, uint32_t sceneIndex) const;

    struct GBufferTargets {
        std::string albedo;
        std::string normal;
        std::string pbr;
        std::string emissive;
        std::string velocity;
        std::string depthStencil;

        std::string outFinalColor;
    };

    struct PostProcessTargets
    {
        std::string finalColor;
        std::string velocity;
        std::string depthStencil; // stencil should be disregarded
    };

    void SetupGeometryPasses(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneIndex, bool
                               bClearTargets) const;

    void SetupGroundTruthAmbientOcclusion(RenderGraph& graph, const Core::ViewFamily& renderViewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const;

    void SetupShadowsResolve(RenderGraph& graph, const Core::ViewFamily& renderViewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const;

    void SetupDeferredLighting(RenderGraph& graph, const Core::ViewFamily& renderViewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const;

    void SetupPortalComposite(RenderGraph& graph, const Core::ViewFamily& renderViewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, const GBufferTargets& portalTargets) const;

    void SetupSkyboxRendering(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const;

    std::string SetupTemporalAntialiasing(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const PostProcessTargets& ppTargets) const;

    std::string SetupPostProcessing(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const PostProcessTargets& ppTargets, float deltaTime) const;

    void SetupDebugRender(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, const std::string& depthTarget, const std::string& targetImage, FrameResourceLimits& limits) const;

public:
#if WILL_EDITOR
    moodycamel::ConcurrentQueue<AssetLoad::GPUDispatchRequest> editorGPUDispatchQueue;
#endif

private:
    // Non-owning
    SDL_Window* window{};
    Core::FrameSync* engineRenderSynchronization{};
    enki::TaskScheduler* scheduler{};

    // Threading
    std::atomic<bool> bShouldExit{false};
    std::jthread thisThread;

    // Owning
    std::unique_ptr<VulkanContext> context{};
    std::unique_ptr<Swapchain> swapchain{};
    std::unique_ptr<ImguiWrapper> imgui{};
    std::unique_ptr<ResourceManager> resourceManager{};
    std::unique_ptr<RenderExtents> renderExtents{};
    std::unique_ptr<RenderGraph> renderGraph{};
    std::unique_ptr<PipelineManager> pipelineManager{};

    std::array<RenderSynchronization, Core::FRAME_BUFFER_COUNT> frameSynchronization;

    std::vector<VkBufferMemoryBarrier2> tempBufferBarriers;
    std::vector<VkImageMemoryBarrier2> tempImageBarriers;

    uint32_t currentFrameInFlight{0};
    uint64_t frameNumber{0};
    RenderFamilyProperties persistentRenderFamilyProperties{}; // so vector can be reused
    FrameResourceLimits frameResourceLimits{};
    bool bEngineRequestsRecreate{false};
    bool bRenderRequestsRecreate{false};
    bool bFrozenVisibility{false};

private:
    PipelineLayout globalPipelineLayout;
};
} // Render

#endif //WILLENGINEV3_RENDER_THREAD_H
