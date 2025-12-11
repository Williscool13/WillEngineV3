//
// Created by William on 2025-12-09.
//

#include "render_thread.h"

#include "vk_context.h"
#include "vk_render_extents.h"
#include "vk_render_targets.h"
#include "vk_swapchain.h"
#include "enkiTS/src/TaskScheduler.h"

namespace Render
{
RenderThread::RenderThread() = default;

RenderThread::~RenderThread() = default;

void RenderThread::Initialize(Engine::WillEngine* engine_, enki::TaskScheduler* scheduler_, SDL_Window* window_, uint32_t width, uint32_t height)
{
    engine = engine_;
    scheduler = scheduler_;
    window = window_;

    context = std::make_unique<VulkanContext>(window);
    swapchain = std::make_unique<Swapchain>(context.get(), width, height);
    renderTargets = std::make_unique<RenderTargets>(context.get(), width, height);
    renderExtents = std::make_unique<RenderExtents>(width, height, 1.0f);
}

void RenderThread::Start()
{
    bShouldExit.store(false, std::memory_order_release);

    uint32_t renderThreadNum = scheduler->GetNumTaskThreads() - 1;
    pinnedTask = std::make_unique<enki::LambdaPinnedTask>(
        renderThreadNum,
        [this]() { ThreadMain(); }
    );

    scheduler->AddPinnedTask(pinnedTask.get());
}

void RenderThread::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
}

void RenderThread::Join()
{
    vkDeviceWaitIdle(context->device);
    if (pinnedTask) {
        scheduler->WaitforTask(pinnedTask.get());
    }
}

void RenderThread::ThreadMain()
{
    while (!bShouldExit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
} // Render
