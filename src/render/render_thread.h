//
// Created by William on 2025-12-09.
//

#ifndef WILLENGINEV3_RENDER_THREAD_H
#define WILLENGINEV3_RENDER_THREAD_H

#include <array>
#include <atomic>
#include <memory>
#include <glm/glm.hpp>

#include "frame_resources.h"
#include "vk_operation_ring_buffer.h"
#include "vk_synchronization.h"
#include "pipelines/basic_compute_pipeline.h"
#include "pipelines/basic_render_pipeline.h"

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
struct ResourceManager;
struct RenderExtents;
struct RenderTargets;
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

    ~RenderThread();

    void Initialize(Core::FrameSync* engineRenderSync, enki::TaskScheduler* scheduler_, SDL_Window* window_, uint32_t width, uint32_t height);

    void Start();

    void RequestShutdown();

    void Join();

    void ThreadMain();

    RenderResponse Render(uint32_t currentFrameIndex, RenderSynchronization& renderSync, Core::FrameBuffer& frameBuffer, FrameResources& frameResource);

    void ProcessBufferOperations(Core::FrameBuffer& frameBuffer, FrameResources& frameResource);

    void ProcessAcquisitions(VkCommandBuffer cmd, Core::FrameBuffer& frameBuffer);

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
    std::unique_ptr<RenderTargets> renderTargets{};
    std::unique_ptr<ResourceManager> resourceManager{};
    std::unique_ptr<RenderExtents> renderExtents{};

    std::array<RenderSynchronization, Core::FRAME_BUFFER_COUNT> frameSynchronization;
    std::array<FrameResources, Core::FRAME_BUFFER_COUNT> frameResources;

    std::vector<VkBufferMemoryBarrier2> tempBufferBarriers;
    std::vector<VkImageMemoryBarrier2> tempImageBarriers;

    ModelMatrixOperationRingBuffer modelMatrixOperationRingBuffer;
    InstanceOperationRingBuffer instanceOperationRingBuffer;
    JointMatrixOperationRingBuffer jointMatrixOperationRingBuffer;


    uint64_t frameNumber{0};
    bool bEngineRequestsRecreate{false};
    bool bRenderRequestsRecreate{false};
    uint32_t highestInstanceIndex{0};
    SceneData sceneData{};

private:
    BasicComputePipeline basicComputePipeline;
    BasicRenderPipeline basicRenderPipeline;
};
} // Render

#endif //WILLENGINEV3_RENDER_THREAD_H