//
// Created by William on 2025-12-17.
//

#include "asset_load_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "will_model_loader.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
AssetLoadThread::AssetLoadThread() = default;

AssetLoadThread::~AssetLoadThread()
{
    if (context) {
        vkDestroyCommandPool(context->device, commandPool, nullptr);
    }
}

void AssetLoadThread::Initialize(enki::TaskScheduler* _scheduler, Render::VulkanContext* _context, Render::ResourceManager* _resourceManager)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(context->transferQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &commandPool));
    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(ASSET_LOAD_ASYNC_COUNT, commandPool);
    std::array<VkCommandBuffer, ASSET_LOAD_ASYNC_COUNT> commandBuffers{};
    VK_CHECK(vkAllocateCommandBuffers(context->device, &cmdInfo, commandBuffers.data()));

    for (int32_t i = 0; i < assetLoadSlots.size(); ++i) {
        assetLoadSlots[i].uploadStaging->Initialize(context, commandBuffers[i]);
    }

    // CreateDefaultResources();
}

void AssetLoadThread::Start()
{
    bShouldExit.store(false, std::memory_order_release);

    uint32_t assetLoadThreadNum = scheduler->GetNumTaskThreads() - 2;
    pinnedTask = std::make_unique<enki::LambdaPinnedTask>(
        assetLoadThreadNum,
        [this] { ThreadMain(); }
    );

    scheduler->AddPinnedTask(pinnedTask.get());
}

void AssetLoadThread::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
}

void AssetLoadThread::Join() const
{
    if (pinnedTask) {
        scheduler->WaitforTask(pinnedTask.get());
    }
}

void AssetLoadThread::RequestLoad(Render::WillModelHandle willmodelHandle)
{
    modelLoadQueue.push({willmodelHandle});
}

void AssetLoadThread::ThreadMain()
{
    while (!bShouldExit.load()) {
        // Try to start
        for (size_t i = 0; i < ASSET_LOAD_ASYNC_COUNT; ++i) {
            if (!loaderActive[i]) {
                WillModelLoadRequest loadRequest{};
                if (modelLoadQueue.pop(loadRequest)) {
                    Render::WillModel* modelToLoad = resourceManager->models.Get(loadRequest.willModelHandle);
                    if (modelToLoad == nullptr) {
                        SPDLOG_ERROR("[AssetLoadThread::Join] Model load failed to obtain the loadable asset");
                        modelCompleteQueue.push({loadRequest.willModelHandle});
                        continue;
                    }

                    assetLoadSlots[i].loadState = WillModelLoadState::Idle;
                    assetLoadSlots[i].willModelHandle = loadRequest.willModelHandle;
                    assetLoadSlots[i].model = modelToLoad;
                    loaderActive[i] = true;
                }
            }
        }

        // Resolve existing currently loading stuff
        for (size_t i = 0; i < ASSET_LOAD_ASYNC_COUNT; ++i) {
            if (loaderActive[i]) {
                WillModelLoader& assetLoad = assetLoadSlots[i];
                switch (assetLoad.loadState) {
                    case WillModelLoadState::Idle:
                    {
                        assetLoad.TaskExecute(scheduler, assetLoad.loadModelTask.get());
                        assetLoad.loadState = WillModelLoadState::TaskExecuting;
                        SPDLOG_INFO("Started task for {}", assetLoad.model->name);
                    }
                    break;
                    case WillModelLoadState::TaskExecuting:
                    {
                        WillModelLoader::TaskState res = assetLoad.TaskExecute(scheduler, assetLoad.loadModelTask.get());
                        if (res == WillModelLoader::TaskState::Failed) {
                            assetLoad.loadState = WillModelLoadState::Failed;
                            SPDLOG_WARN("Failed task for {}", assetLoad.model->name);
                        }
                        else if (res == WillModelLoader::TaskState::Complete) {
                            assetLoad.ThreadExecute(context, resourceManager);
                            assetLoad.loadState = WillModelLoadState::ThreadExecuting;
                            SPDLOG_INFO("Started thread for {}", assetLoad.model->name);
                        }
                    }
                    break;
                    case WillModelLoadState::ThreadExecuting:
                    {
                        WillModelLoader::ThreadState res = assetLoad.ThreadExecute(context, resourceManager);
                        if (res == WillModelLoader::ThreadState::Failed) {
                            assetLoad.loadState = WillModelLoadState::Failed;
                            SPDLOG_WARN("Failed thread execute for {}", assetLoad.model->name);
                        }
                        else if (res == WillModelLoader::ThreadState::Complete) {
                            const bool postRes = assetLoad.PostThreadExecute(context, resourceManager);
                            if (postRes) {
                                assetLoad.loadState = WillModelLoadState::Loaded;
                                SPDLOG_INFO("Successfully loaded {}", assetLoad.model->name);
                            }
                            else {
                                assetLoad.loadState = WillModelLoadState::Failed;
                                SPDLOG_INFO("Failed post thread execute for {}", assetLoad.model->name);
                            }
                        }
                    }
                    break;
                    default:
                        break;
                }

                if (assetLoad.loadState == WillModelLoadState::Loaded || assetLoad.loadState == WillModelLoadState::Failed) {
                    assetLoad.taskState = WillModelLoader::TaskState::NotStarted;
                    assetLoad.threadState = WillModelLoader::ThreadState::NotStarted;
                    modelCompleteQueue.push({assetLoad.willModelHandle});
                    loaderActive[i] = false;

                    assetLoad.Reset();
                }
            }
        }
    }
}
} // AssetLoad
