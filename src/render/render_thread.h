//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_RENDER_THREAD_H
#define WILLENGINEV3_RENDER_THREAD_H

#include <array>
#include <atomic>
#include <memory>

#include "core/containers/vector.h"
#include "frame_resources.h"
#include "asset-load/async_asset_load_manager.h"
#include "core/containers/array.h"
#include "interface/render_interface.h"
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

    void InitializePipelineManager(AssetLoad::AsyncAssetLoadManager* _asyncAssetLoadManager);

    void Start();

    void RequestShutdown();

    void Join();

    void ThreadMain();

    void RenderFrame(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer);

    RenderResponse RecordFrame(uint32_t frameIndex, VkCommandBuffer cmd, VkSemaphore swapchainSemaphore, Core::FrameBuffer& frameBuffer);

    void ProcessAcquisitions(VkCommandBuffer cmd, const std::vector<Core::BufferAcquireOperation>& bufferAcquireOperations, const std::vector<Core::ImageAcquireOperation>& imageAcquireOperations);

public:
    VulkanContext* GetVulkanContext() const { return context; }
    ResourceManager* GetResourceManager() const { return resourceManager; }
    PipelineManager* GetPipelineManager() const { return pipelineManager; }

private:
    void CreatePipelines();

    void PrepareRenderFamilyProperties(Core::ViewFamily& viewFamily, ReadbackStruct* readbackData, RenderFamilyProperties& renderFamilyProperties, PipelineManager* _pipelineManager, FrameResourceLimits& _limits);

    void UploadFrameUniforms(const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, float renderDeltaTime) const;

    void UploadModelUniforms(Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties) const;

    void SetupCascadedShadows(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties, uint32_t sceneIndex) const;

    struct GBufferTargets {
        StringID albedo;
        StringID normal;
        StringID pbr;
        StringID emissive;
        StringID velocity;
        StringID depthStencil;

        StringID stableId;

        StringID outFinalColor;
    };

    struct PostProcessTargets
    {
        StringID finalColor;
        StringID velocity;
        StringID depthStencil; // stencil should be disregarded
    };

    void SetupGeometryPasses(RenderGraph& graph, const Core::ViewFamily& viewFamily, const RenderFamilyProperties& renderFamilyProperties, Core::Array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneIndex, bool
                               bClearTargets) const;

    void SetupGroundTruthAmbientOcclusion(RenderGraph& graph, const Core::ViewFamily& renderViewFamily, Core::Array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const;

    void SetupShadowsResolve(RenderGraph& graph, const Core::ViewFamily& renderViewFamily, Core::Array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const;

    void SetupDeferredLighting(RenderGraph& graph, const Core::ViewFamily& renderViewFamily, Core::Array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const;

    void SetupPortalComposite(RenderGraph& graph, const Core::ViewFamily& renderViewFamily, Core::Array<uint32_t, 2> renderExtent, const GBufferTargets& targets, const GBufferTargets& portalTargets) const;

    void SetupSkyboxRendering(RenderGraph& graph, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const GBufferTargets& targets, uint32_t sceneDataIndex) const;

    StringID SetupTemporalAntialiasing(RenderGraph& graph, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const PostProcessTargets& ppTargets) const;

    StringID SetupPostProcessing(RenderGraph& graph, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const PostProcessTargets& ppTargets, float deltaTime) const;

    void SetupDebugRender(RenderGraph& graph, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, StringID depthTarget, StringID targetImage, FrameResourceLimits& limits) const;

public:
#if WILL_EDITOR
    moodycamel::ConcurrentQueue<AssetLoad::GPUDispatchRequest> editorGPUDispatchQueue;
#endif

private:
    // Non-owning
    Core::MemoryManager* memoryManager{};
    SDL_Window* window{};
    Core::FrameSync* engineRenderSynchronization{};
    enki::TaskScheduler* scheduler{};

    // Threading
    std::atomic<bool> bShouldExit{false};
    std::jthread thisThread;

    // Owning
    VulkanContext*   context{};
    Swapchain*       swapchain{};
    ImguiWrapper*    imgui{};
    ResourceManager* resourceManager{};
    RenderExtents*   renderExtents{};
    PipelineManager* pipelineManager{};

    // todo: refactor
    std::unique_ptr<RenderGraph> renderGraph{};

    Core::Array<RenderSynchronization, Core::FRAME_BUFFER_COUNT> frameSynchronization;

    Core::Vector<VkBufferMemoryBarrier2> tempBufferBarriers;
    Core::Vector<VkImageMemoryBarrier2>  tempImageBarriers;

    uint32_t currentFrameInFlight{0};
    uint64_t frameNumber{0};
    // todo remove
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
