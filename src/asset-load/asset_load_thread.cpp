//
// Created by William on 2025-12-17.
//

#include "asset_load_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

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

        for (UploadStaging& uploadStaging : uploadStagingDatas) {
            vkDestroyFence(context->device, uploadStaging.fence, nullptr);
        }
    }
}

void AssetLoadThread::Initialize(enki::TaskScheduler* _scheduler, Render::VulkanContext* _context, Render::ResourceManager* _resourceManager)
{
    scheduler = _scheduler;
    context = _context;
    resourceManager = _resourceManager;

    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(context->transferQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &commandPool));
    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(4, commandPool);
    std::array<VkCommandBuffer, ASSET_LOAD_ASYNC_COUNT> commandBuffers{};
    VK_CHECK(vkAllocateCommandBuffers(context->device, &cmdInfo, commandBuffers.data()));

    for (int32_t i = 0; i < uploadStagingDatas.size(); ++i) {
        VkFenceCreateInfo fenceInfo = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = 0, // Unsignaled
        };
        VK_CHECK(vkCreateFence(context->device, &fenceInfo, nullptr, &uploadStagingDatas[i].fence));
        uploadStagingDatas[i].commandBuffer = commandBuffers[i];
        uploadStagingDatas[i].stagingBuffer = Render::AllocatedBuffer::CreateAllocatedStagingBuffer(context, ASSET_LOAD_STAGING_BUFFER_SIZE);
    }

    // CreateDefaultResources();
}

void AssetLoadThread::Start()
{
    bShouldExit.store(false, std::memory_order_release);

    uint32_t renderThreadNum = scheduler->GetNumTaskThreads() - 1;
    pinnedTask = std::make_unique<enki::LambdaPinnedTask>(
        renderThreadNum,
        [this] { ThreadMain(); }
    );

    scheduler->AddPinnedTask(pinnedTask.get());
}

void AssetLoadThread::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
}

void AssetLoadThread::Join()
{
    if (pinnedTask) {
        scheduler->WaitforTask(pinnedTask.get());
    }
}

void AssetLoadThread::ThreadMain()
{
    while (!bShouldExit.load()) {
        // bool didWork = false;
        //
        // FinishUploadsInProgress();
        //
        // for (int i = static_cast<int>(modelsInProgress.size()) - 1; i >= 0; --i) {
        //     AssetLoadInProgress& inProgress = modelsInProgress[i];
        //     ModelEntry* modelEntry = models.Get(inProgress.modelEntryHandle);
        //
        //     if (!modelEntry) {
        //         LOG_ERROR("Model handle became invalid while loading");
        //         modelsInProgress.erase(modelsInProgress.begin() + i);
        //         didWork = true;
        //         continue;
        //     }
        //
        //
        //     RemoveFinishedUploadStaging(modelEntry->uploadStagingHandles);
        //
        //     if (modelEntry->uploadStagingHandles.empty()) {
        //         if (modelEntry->state.load() != ModelEntry::State::Ready) {
        //             modelEntry->loadEndTime = std::chrono::steady_clock::now();
        //             modelEntry->state.store(ModelEntry::State::Ready);
        //
        //             auto duration = std::chrono::duration_cast<std::chrono::microseconds>(modelEntry->loadEndTime - modelEntry->loadStartTime);
        //             LOG_INFO("[Asset Loading Thread] Model '{}' loaded in {:.3f} ms", modelEntry->data.name, duration.count() / 1000.0);
        //         }
        //
        //         completeQueue.push({inProgress.modelEntryHandle, std::move(inProgress.onComplete)});
        //         modelsInProgress.erase(modelsInProgress.begin() + i);
        //         didWork = true;
        //     }
        // }
        //
        // if (uploadStagingHandleAllocator.IsAnyFree()) {
        //     AssetLoadRequest loadRequest;
        //     while (requestQueue.pop(loadRequest)) {
        //         ModelEntryHandle newModelHandle = LoadGltf(loadRequest.path);
        //         if (newModelHandle == ModelEntryHandle::Invalid) {
        //             completeQueue.push({newModelHandle, std::move(loadRequest.onComplete)});
        //         }
        //         else {
        //             modelsInProgress.push_back({newModelHandle, std::move(loadRequest.onComplete)});
        //         }
        //         didWork = true;
        //     }
        // }
        //
        // if (!didWork) {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // }
    }
}
} // AssetLoad
