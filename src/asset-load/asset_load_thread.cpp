//
// Created by William on 2025-12-17.
//

#include "asset_load_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "will_model_loader.h"
#include "render/model/will_model_asset.h"
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

void AssetLoadThread::RequestLoad(Engine::WillModelHandle willmodelHandle, Render::WillModel* willModelPtr)
{
    modelLoadQueue.push({willmodelHandle, willModelPtr});
}

bool AssetLoadThread::ResolveLoads(WillModelComplete& modelComplete)
{
    return modelCompleteLoadQueue.pop(modelComplete);
}

void AssetLoadThread::RequestUnLoad(Engine::WillModelHandle willmodelHandle, Render::WillModel* willModelPtr)
{
    modelUnloadQueue.push({willmodelHandle, willModelPtr});
}

bool AssetLoadThread::ResolveUnload(WillModelComplete& modelComplete)
{
    return modelCompleteUnloadQueue.pop(modelComplete);
}

void AssetLoadThread::ThreadMain()
{
    while (!bShouldExit.load(std::memory_order_acquire)) {
        // Try to start
        for (size_t i = 0; i < ASSET_LOAD_ASYNC_COUNT; ++i) {
            if (!loaderActive[i]) {
                WillModelLoadRequest loadRequest{};
                if (modelLoadQueue.pop(loadRequest)) {
                    assetLoadSlots[i].loadState = WillModelLoadState::Idle;
                    assetLoadSlots[i].willModelHandle = loadRequest.willModelHandle;
                    assetLoadSlots[i].model = loadRequest.model;
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
                            SPDLOG_INFO("Finished task for {}", assetLoad.model->name);
                            const bool preRes = assetLoad.PreThreadExecute(context, resourceManager);
                            if (preRes) {
                                assetLoad.loadState = WillModelLoadState::ThreadExecuting;
                                assetLoad.ThreadExecute(context, resourceManager);
                                SPDLOG_INFO("Started thread execute for {}", assetLoad.model->name);
                            }
                            else {
                                assetLoad.loadState = WillModelLoadState::Failed;
                                SPDLOG_INFO("Failed pre thread execute for {}", assetLoad.model->name);
                            }
                        }
                    }
                    break;
                    case WillModelLoadState::ThreadExecuting:
                    {
                        WillModelLoader::ThreadState res = assetLoad.ThreadExecute(context, resourceManager);
                        if (res == WillModelLoader::ThreadState::Complete) {
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
                    bool success = assetLoad.loadState == WillModelLoadState::Loaded;
                    modelCompleteLoadQueue.push({assetLoad.willModelHandle, assetLoad.model, success});
                    loaderActive[i] = false;

                    assetLoad.Reset();
                }
            }
        }


        // Resolve unloads
        WillModelLoadRequest unloadRequest{};
        if (modelUnloadQueue.pop(unloadRequest)) {
            OffsetAllocator::Allocator* selectedAllocator;
            if (unloadRequest.model->modelData.bIsSkinned) {
                selectedAllocator = &resourceManager->skinnedVertexBufferAllocator;
            }
            else {
                selectedAllocator = &resourceManager->vertexBufferAllocator;
            }

            selectedAllocator->free(unloadRequest.model->modelData.vertexAllocation);
            resourceManager->meshletVertexBufferAllocator.free(unloadRequest.model->modelData.meshletVertexAllocation);
            resourceManager->meshletTriangleBufferAllocator.free(unloadRequest.model->modelData.meshletTriangleAllocation);
            resourceManager->meshletBufferAllocator.free(unloadRequest.model->modelData.meshletAllocation);
            resourceManager->primitiveBufferAllocator.free(unloadRequest.model->modelData.primitiveAllocation);
            unloadRequest.model->modelData.vertexAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.meshletVertexAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.meshletTriangleAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.meshletAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;
            unloadRequest.model->modelData.primitiveAllocation.metadata = OffsetAllocator::Allocation::NO_SPACE;

            modelCompleteUnloadQueue.push({unloadRequest.willModelHandle, unloadRequest.model, true});
        }
    }
}
} // AssetLoad
