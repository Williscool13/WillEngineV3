//
// Created by William on 2025-12-17.
//

#include "asset_load_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "asset_load_job.h"
#include "texture_load_job.h"
#include "will_model_load_job.h"
#include "platform/paths.h"
#include "platform/thread_utils.h"
#include "render/texture_asset.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
AssetLoadThread::AssetLoadThread() = default;

AssetLoadThread::AssetLoadThread(enki::TaskScheduler* scheduler, Render::VulkanContext* context, Render::ResourceManager* resourceManager)
    : context(context), resourceManager(resourceManager), scheduler(scheduler)
{
    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(context->transferQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &commandPool));

    const uint32_t totalCommandBuffers = WILL_MODEL_JOB_COUNT + TEXTURE_JOB_COUNT + 1;
    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(totalCommandBuffers, commandPool);
    std::vector<VkCommandBuffer> commandBuffers(totalCommandBuffers);
    VK_CHECK(vkAllocateCommandBuffers(context->device, &cmdInfo, commandBuffers.data()));


    willModelJobs.reserve(WILL_MODEL_JOB_COUNT);
    for (int32_t i = 0; i < WILL_MODEL_JOB_COUNT; ++i) {
        willModelJobs.emplace_back(std::make_unique<WillModelLoadJob>(context, resourceManager, commandBuffers[i]));
    }

    textureJobs.reserve(TEXTURE_JOB_COUNT);
    for (int32_t i = 0; i < TEXTURE_JOB_COUNT; ++i) {
        textureJobs.emplace_back(std::make_unique<TextureLoadJob>(context, resourceManager, commandBuffers[WILL_MODEL_JOB_COUNT + i]));
    }
}

AssetLoadThread::~AssetLoadThread()
{
    if (context) {
        vkDestroyCommandPool(context->device, commandPool, nullptr);
    }
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

void AssetLoadThread::RequestTextureLoad(Engine::TextureHandle textureHandle, Render::Texture* texturePtr)
{
    textureLoadQueue.push({textureHandle, texturePtr});
}

bool AssetLoadThread::ResolveTextureLoads(TextureComplete& textureComplete)
{
    return textureCompleteLoadQueue.pop(textureComplete);
}

void AssetLoadThread::RequestTextureUnload(Engine::TextureHandle textureHandle, Render::Texture* texturePtr)
{
    textureUnloadQueue.push({textureHandle, texturePtr});
}

bool AssetLoadThread::ResolveTextureUnload(TextureComplete& textureComplete)
{
    return textureCompleteUnloadQueue.pop(textureComplete);
}

Render::Sampler AssetLoadThread::CreateSampler(const VkSamplerCreateInfo& samplerCreateInfo) const
{
    return Render::Sampler::CreateSampler(context, samplerCreateInfo);
}

void AssetLoadThread::ThreadMain()
{
    Platform::SetThreadName("AssetLoadThread");
    while (!bShouldExit.load(std::memory_order_acquire)) {
        bool didWork = false;

        // Model loading jobs
        {
            // Count free model load jobs (4 max)
            size_t freeJobCount = 0;
            for (size_t i = 0; i < willModelJobActive.size(); ++i) {
                if (!willModelJobActive[i]) {
                    freeJobCount++;
                }
            }

            // Only pop as many requests as we have free jobs
            for (size_t jobsStarted = 0; jobsStarted < freeJobCount; ++jobsStarted) {
                WillModelLoadRequest loadRequest{};
                if (!modelLoadQueue.pop(loadRequest)) {
                    break;
                }
                didWork = true;

                int32_t slotIdx = -1;
                for (size_t i = 0; i < 64; ++i) {
                    if (!(activeSlotMask[i])) {
                        slotIdx = i;
                        break;
                    }
                }

                // Find free job (guaranteed to exist)
                int32_t freeJobIdx = -1;
                for (size_t i = 0; i < willModelJobActive.size(); ++i) {
                    if (!willModelJobActive[i]) {
                        freeJobIdx = i;
                        break;
                    }
                }

                WillModelLoadJob* job = willModelJobs[freeJobIdx].get();
                job->willModelHandle = loadRequest.willModelHandle;
                job->outputModel = loadRequest.model;
                willModelJobActive[freeJobIdx] = true;

                assetLoadSlots[slotIdx].job = job;
                assetLoadSlots[slotIdx].loadState = AssetLoadState::Idle;
                assetLoadSlots[slotIdx].type = AssetType::WillModel;
                activeSlotMask[slotIdx] = true;
            }
        }

        // Texture loading jobs
        {
            size_t freeTextureJobCount = 0;
            for (size_t i = 0; i < textureJobActive.size(); ++i) {
                if (!textureJobActive[i]) {
                    freeTextureJobCount++;
                }
            }

            for (size_t jobsStarted = 0; jobsStarted < freeTextureJobCount; ++jobsStarted) {
                TextureLoadRequest loadRequest{};
                if (!textureLoadQueue.pop(loadRequest)) {
                    break;
                }
                didWork = true;

                int32_t slotIdx = -1;
                for (size_t i = 0; i < 64; ++i) {
                    if (!(activeSlotMask[i])) {
                        slotIdx = i;
                        break;
                    }
                }

                int32_t freeJobIdx = -1;
                for (size_t i = 0; i < textureJobActive.size(); ++i) {
                    if (!textureJobActive[i]) {
                        freeJobIdx = i;
                        break;
                    }
                }

                TextureLoadJob* job = textureJobs[freeJobIdx].get();
                job->textureHandle = loadRequest.textureHandle;
                job->outputTexture = loadRequest.texture;
                textureJobActive[freeJobIdx] = true;

                assetLoadSlots[slotIdx].job = job;
                assetLoadSlots[slotIdx].loadState = AssetLoadState::Idle;
                assetLoadSlots[slotIdx].type = AssetType::Texture;
                activeSlotMask[slotIdx] = true;
            }
        }

        // Active Slot Processing
        for (size_t slotIdx = 0; slotIdx < 64; ++slotIdx) {
            if (!activeSlotMask[slotIdx]) {
                continue;
            }
            didWork = true;

            AssetLoadSlot& slot = assetLoadSlots[slotIdx];
            AssetLoadJob* job = slot.job;

            switch (slot.loadState) {
                case AssetLoadState::Idle:
                {
                    job->StartJob();
                    job->TaskExecute(scheduler);
                    slot.loadState = AssetLoadState::TaskExecuting;
                }
                break;

                case AssetLoadState::TaskExecuting:
                {
                    TaskState res = job->TaskExecute(scheduler);
                    if (res == TaskState::Failed) {
                        slot.loadState = AssetLoadState::Failed;
                    }
                    else if (res == TaskState::Complete) {
                        bool preRes = job->PreThreadExecute();
                        if (preRes) {
                            slot.loadState = AssetLoadState::ThreadExecuting;
                        }
                        else {
                            slot.loadState = AssetLoadState::Failed;
                        }
                    }
                }
                break;

                case AssetLoadState::ThreadExecuting:
                {
                    ThreadState res = job->ThreadExecute();
                    if (res == ThreadState::Complete) {
                        bool postRes = job->PostThreadExecute();
                        if (postRes) {
                            slot.loadState = AssetLoadState::Loaded;
                        }
                        else {
                            slot.loadState = AssetLoadState::Failed;
                        }
                    }
                }
                break;

                default:
                    break;
            }

            if (slot.loadState == AssetLoadState::Loaded || slot.loadState == AssetLoadState::Failed) {
                bool success = slot.loadState == AssetLoadState::Loaded;

                switch (slot.type) {
                    case AssetType::WillModel:
                    {
                        auto* modelJob = static_cast<WillModelLoadJob*>(job);
                        modelCompleteLoadQueue.push({modelJob->willModelHandle, modelJob->outputModel, success});

                        for (size_t i = 0; i < willModelJobs.size(); ++i) {
                            if (willModelJobs[i].get() == job) {
                                job->Reset();
                                willModelJobActive[i] = false;
                                break;
                            }
                        }
                        break;
                    }
                    case AssetType::Texture:
                    {
                        auto* textureJob = static_cast<TextureLoadJob*>(job);
                        textureCompleteLoadQueue.push({textureJob->textureHandle, textureJob->outputTexture, success});

                        for (size_t i = 0; i < textureJobs.size(); ++i) {
                            if (textureJobs[i].get() == job) {
                                job->Reset();
                                textureJobActive[i] = false;
                                break;
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }

                activeSlotMask[slotIdx] = false;
                slot.job = nullptr;
                slot.loadState = AssetLoadState::Unassigned;
                slot.type = AssetType::None;
            }
        }


        WillModelLoadRequest unloadRequest{};
        if (modelUnloadQueue.pop(unloadRequest)) {
            didWork = true;
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

        TextureLoadRequest textureUnloadRequest{};
        if (textureUnloadQueue.pop(textureUnloadRequest)) {
            didWork = true;

            textureUnloadRequest.texture->image = {};
            textureUnloadRequest.texture->imageView = {};

            textureCompleteUnloadQueue.push({textureUnloadRequest.textureHandle, textureUnloadRequest.texture, true});
        }

        if (!didWork) {
            std::chrono::microseconds idleWait = std::chrono::microseconds(10);
            std::this_thread::sleep_for(idleWait);
        }
    }
}
} // AssetLoad
