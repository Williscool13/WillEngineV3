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
#include "pipelines/basic_compute_pipeline.h"
#include "pipelines/basic_render_pipeline.h"
#include "pipelines/mesh_shader_pipeline.h"
#include "shaders/common_interop.h"

namespace Render
{
class RenderGraph;
}

namespace Core
{
class FrameSync;
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

#if WILL_EDITOR
struct ImguiWrapper;
#endif
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

    void Start();

    void RequestShutdown();

    void Join();

    void ThreadMain();

    RenderResponse Render(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer, FrameResources& frameResource);

    void ProcessAcquisitions(VkCommandBuffer cmd, Core::FrameBuffer& frameBuffer);

public:
    VulkanContext* GetVulkanContext() const { return context.get(); }
    ResourceManager* GetResourceManager() const { return resourceManager.get(); }

private:
    // Non-owning
    SDL_Window* window{};
    Core::FrameSync* engineRenderSynchronization{};
    enki::TaskScheduler* scheduler{};

    // Threading
    std::atomic<bool> bShouldExit{false};
    std::unique_ptr<enki::LambdaPinnedTask> pinnedTask{};

    // Owning
    std::unique_ptr<VulkanContext> context{};
    std::unique_ptr<Swapchain> swapchain{};
#if WILL_EDITOR
    std::unique_ptr<ImguiWrapper> imgui{};
#endif
    std::unique_ptr<ResourceManager> resourceManager{};
    std::unique_ptr<RenderExtents> renderExtents{};
    std::unique_ptr<RenderGraph> graph{};

    std::array<RenderSynchronization, Core::FRAME_BUFFER_COUNT> frameSynchronization;

    std::vector<VkBufferMemoryBarrier2> tempBufferBarriers;
    std::vector<VkImageMemoryBarrier2> tempImageBarriers;

    uint32_t currentFrameInFlight{0};
    uint64_t frameNumber{0};
    bool bEngineRequestsRecreate{false};
    bool bRenderRequestsRecreate{false};
    int32_t highestInstanceIndex{-1};
    SceneData sceneData{};

private:
    BasicComputePipeline basicComputePipeline;
    BasicRenderPipeline basicRenderPipeline;
    MeshShaderPipeline meshShaderPipeline;
};
} // Render

#endif //WILLENGINEV3_RENDER_THREAD_H
