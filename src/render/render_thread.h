//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_RENDER_THREAD_H
#define WILLENGINEV3_RENDER_THREAD_H

#include <array>
#include <atomic>
#include <memory>

#include "frame_resources.h"
#include "core/include/render_interface.h"
#include "render/vulkan/vk_synchronization.h"
#include "pipelines/compute_pipeline.h"
#include "pipelines/mesh_shading_instanced_pipeline.h"
#include "pipelines/shadow_mesh_shading_instanced_pipeline.h"
#include "shaders/common_interop.h"

namespace AssetLoad
{
class AssetLoadThread;
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
class AssetGenerator;
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
    enum RenderResponse
    {
        SUCCESS,
        SWAPCHAIN_OUTDATED
    };

public:
    RenderThread();

    RenderThread(Core::FrameSync* engineRenderSynchronization, enki::TaskScheduler* scheduler, SDL_Window* window, uint32_t width, uint32_t height);

    ~RenderThread();

    void InitializePipelineManager(AssetLoad::AssetLoadThread* assetLoadThread);

    void Start();

    void RequestShutdown();

    void Join() const;

    void ThreadMain();

    RenderResponse Render(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer, FrameResources& frameResource);

    void ProcessAcquisitions(VkCommandBuffer cmd, const std::vector<Core::BufferAcquireOperation>& bufferAcquireOperations, const std::vector<Core::ImageAcquireOperation>& imageAcquireOperations);

public:
    VulkanContext* GetVulkanContext() const { return context.get(); }
    ResourceManager* GetResourceManager() const { return resourceManager.get(); }
    PipelineManager* GetPipelineManager() const { return pipelineManager.get(); }

private:
    void CreatePipelines();

    void SetupFrameUniforms(VkCommandBuffer cmd, const Core::ViewFamily& viewFamily, FrameResources& frameResource, std::array<uint32_t, 2> renderExtent) const;

    void SetupCascadedShadows(RenderGraph& graph, const Core::ViewFamily& viewFamily) const;

    void SetupInstancing(RenderGraph& graph, const Core::ViewFamily& viewFamily) const;

    void SetupMainGeometryPass(RenderGraph& graph, const Core::ViewFamily& viewFamily) const;

    void SetupShadowsResolve(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent) const;

    void SetupDeferredLighting(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent) const;

    void SetupGroundTruthAmbientOcclusion(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent) const;

    void SetupTemporalAntialiasing(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent) const;

    void SetupPostProcessing(RenderGraph& graph, const Core::ViewFamily& viewFamily, std::array<uint32_t, 2> renderExtent, float deltaTime) const;

private:
    // Non-owning
    SDL_Window* window{};
    Core::FrameSync* engineRenderSynchronization{};
    enki::TaskScheduler* scheduler{};
    AssetLoad::AssetLoadThread* assetLoadThread{};

    // Threading
    std::atomic<bool> bShouldExit{false};
    std::unique_ptr<enki::LambdaPinnedTask> pinnedTask{};

    // Owning
    std::unique_ptr<VulkanContext> context{};
    std::unique_ptr<Swapchain> swapchain{};
    std::unique_ptr<ImguiWrapper> imgui{};
    std::unique_ptr<ResourceManager> resourceManager{};
    std::unique_ptr<RenderExtents> renderExtents{};
    std::unique_ptr<RenderGraph> renderGraph{};
    std::unique_ptr<PipelineManager> pipelineManager{};

    std::array<RenderSynchronization, Core::FRAME_BUFFER_COUNT> frameSynchronization;
    std::array<FrameResources, Core::FRAME_BUFFER_COUNT> frameResources;

    std::vector<VkBufferMemoryBarrier2> tempBufferBarriers;
    std::vector<VkImageMemoryBarrier2> tempImageBarriers;

    uint32_t currentFrameInFlight{0};
    uint64_t frameNumber{0};
    bool bEngineRequestsRecreate{false};
    bool bRenderRequestsRecreate{false};
    bool bFrozenVisibility{false};

private:
    PipelineLayout globalPipelineLayout;
    MeshShadingInstancedPipeline meshShadingInstancedPipeline;
    ShadowMeshShadingInstancedPipeline shadowMeshShadingInstancedPipeline;
};
} // Render

#endif //WILLENGINEV3_RENDER_THREAD_H
