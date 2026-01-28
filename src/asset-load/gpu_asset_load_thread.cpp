//
// Created by William on 2025-12-17.
//

#include "gpu_asset_load_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "asset-load-jobs/pipeline_load_slot.h"
#include "asset-load-jobs/texture_load_job.h"
#include "asset-load-jobs/will_model_load_job.h"

#include "platform/thread_utils.h"
#include "render/texture_asset.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
GpuAssetUploadThread::GpuAssetUploadThread() = default;

GpuAssetUploadThread::GpuAssetUploadThread(enki::TaskScheduler* scheduler, Render::VulkanContext* context, Render::ResourceManager* resourceManager, Render::PipelineManager* pipelineManager)
    : context(context), resourceManager(resourceManager), scheduler(scheduler)
{
    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(context->transferQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &commandPool));

    constexpr uint32_t totalCommandBuffers = WILL_MODEL_JOB_COUNT + TEXTURE_JOB_COUNT + 1;
    VkCommandBufferAllocateInfo cmdInfo = Render::VkHelpers::CommandBufferAllocateInfo(totalCommandBuffers, commandPool);
    std::vector<VkCommandBuffer> commandBuffers(totalCommandBuffers);
    VK_CHECK(vkAllocateCommandBuffers(context->device, &cmdInfo, commandBuffers.data()));

    textureJobs.reserve(TEXTURE_JOB_COUNT);
    for (int32_t i = 0; i < TEXTURE_JOB_COUNT; ++i) {
        textureJobs.emplace_back(std::make_unique<TextureLoadJob>(context, resourceManager, commandBuffers[WILL_MODEL_JOB_COUNT + i]));
    }
}

GpuAssetUploadThread::~GpuAssetUploadThread()
{
    if (context) {
        vkDestroyCommandPool(context->device, commandPool, nullptr);
    }
}

void GpuAssetUploadThread::Start()
{
    thisThread = std::jthread([this] { ThreadMain(); });
}

void GpuAssetUploadThread::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void GpuAssetUploadThread::Join()
{
    thisThread.join();
}

void GpuAssetUploadThread::RequestTextureLoad(Engine::TextureHandle textureHandle, Render::Texture* texturePtr)
{
    ZoneScoped;

    // textureLoadQueue.enqueue({textureHandle, texturePtr});
    // workCounter.fetch_add(1);
    // wakeCV.notify_one();
}

bool GpuAssetUploadThread::ResolveTextureLoads(TextureComplete& textureComplete)
{
    return textureCompleteLoadQueue.try_dequeue(textureComplete);
}

void GpuAssetUploadThread::RequestTextureUnload(Engine::TextureHandle textureHandle, Render::Texture* texturePtr)
{
    ZoneScoped;

    textureUnloadQueue.enqueue({textureHandle, texturePtr});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool GpuAssetUploadThread::ResolveTextureUnload(TextureComplete& textureComplete)
{
    return textureCompleteUnloadQueue.try_dequeue(textureComplete);
}

Render::Sampler GpuAssetUploadThread::CreateSampler(const VkSamplerCreateInfo& samplerCreateInfo) const
{
    return Render::Sampler::CreateSampler(context, samplerCreateInfo);
}

void GpuAssetUploadThread::ThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("GPUAssetLoadThread");
    Platform::SetThreadName("GPUAssetLoadThread");

    scheduler->RegisterExternalTaskThread();

    while (!bShouldExit.load(std::memory_order_acquire)) {
        ZoneScopedN("AssetLoadLoop");
        bool didWork = false;

        // Texture loading jobs
        {
            ZoneScopedN("TextureJobDispatch");
            size_t freeTextureJobCount = 0;
            for (size_t i = 0; i < textureJobActive.size(); ++i) {
                if (!textureJobActive[i]) {
                    freeTextureJobCount++;
                }
            }

            for (size_t jobsStarted = 0; jobsStarted < freeTextureJobCount; ++jobsStarted) {
                TextureLoadRequest loadRequest{};
                if (!textureLoadQueue.try_dequeue(loadRequest)) {
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

                assetLoadSlots[slotIdx].name = loadRequest.texture->name;
                assetLoadSlots[slotIdx].job = job;
                assetLoadSlots[slotIdx].loadState = AssetLoadState::Idle;
                assetLoadSlots[slotIdx].type = AssetType::Texture;
                assetLoadSlots[slotIdx].startTime = std::chrono::steady_clock::now();
                assetLoadSlots[slotIdx].uploadCount = 0;
                activeSlotMask[slotIdx] = true;
            }
        }

        // Active Slot Processing
        {
            ZoneScopedN("ProcessSlots");
            for (size_t slotIdx = 0; slotIdx < 64; ++slotIdx) {
                if (!activeSlotMask[slotIdx]) {
                    continue;
                }
                ZoneScopedN("ProcessSlot");
                didWork = true;

                AssetLoadSlot& slot = assetLoadSlots[slotIdx];
                AssetLoadJob* job = slot.job;

                switch (slot.loadState) {
                    case AssetLoadState::Idle:
                    {
                        ZoneScopedN("StartJob");
                        job->StartJob();
                        slot.loadState = AssetLoadState::TaskExecuting;
                    }
                    // Fallthrough
                    case AssetLoadState::TaskExecuting:
                    {
                        ZoneScopedN("TaskExecute");
                        TaskState res = job->TaskExecute(scheduler);
                        if (res == TaskState::Failed) {
                            slot.loadState = AssetLoadState::Failed;
                        }
                        else if (res == TaskState::Complete) {
                            ZoneScopedN("PreThreadExecute");
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
                        ZoneScopedN("ThreadExecute");
                        ThreadState res = job->ThreadExecute();
                        if (res == ThreadState::Complete) {
                            ZoneScopedN("PostThreadExecute");
                            bool postRes = job->PostThreadExecute();
                            slot.uploadCount = job->GetUploadCount();
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
                    ZoneScopedN("CompleteSlot");
                    auto duration = std::chrono::steady_clock::now() - slot.startTime;
                    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

                    bool success = slot.loadState == AssetLoadState::Loaded;
                    switch (slot.type) {
                        case AssetType::Texture:
                        {
                            ZoneScopedN("CompleteTexture");
                            auto* textureJob = dynamic_cast<TextureLoadJob*>(job);
                            //
                            {
                                ZoneScopedN("PushCompleteQueue");
                                textureCompleteLoadQueue.try_enqueue({textureJob->textureHandle, textureJob->outputTexture, success});
                            }
                            //
                            {
                                ZoneScopedN("FindAndResetJob");
                                for (size_t i = 0; i < textureJobs.size(); ++i) {
                                    if (textureJobs[i].get() == job) {
                                        job->Reset();
                                        textureJobActive[i] = false;
                                        break;
                                    }
                                }
                            }

                            if (success) {
                                SPDLOG_INFO("'{}' texture loaded in {}ms with {} uploads", slot.name, durationMs, slot.uploadCount);
                            }
                            else {
                                SPDLOG_INFO("'{}' texture failed to load in {}ms with {} uploads", slot.name, durationMs, slot.uploadCount);
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    //
                    {
                        ZoneScopedN("ClearSlot");
                        activeSlotMask[slotIdx] = false;
                        slot.job = nullptr;
                        slot.loadState = AssetLoadState::Unassigned;
                        slot.type = AssetType::None;
                        slot.uploadCount = 0;
                    }
                }
            }
        }


        // Unloads
        {
            ZoneScopedN("ProcessUnloads");
            TextureLoadRequest textureUnloadRequest{};
            if (textureUnloadQueue.try_dequeue(textureUnloadRequest)) {
                didWork = true;

                textureUnloadRequest.texture->image = {};
                textureUnloadRequest.texture->imageView = {};

                textureCompleteUnloadQueue.try_enqueue({textureUnloadRequest.textureHandle, textureUnloadRequest.texture, true});
            }
        }


        if (workCounter.load(std::memory_order_acquire) > 0) {
            workCounter.fetch_sub(1);
        }
        else {
            ZoneScopedN("Idle - Waiting for Work");
            std::unique_lock lock(wakeMutex);
            wakeCV.wait(lock, [&] {
                return workCounter.load(std::memory_order_acquire) > 0 || bShouldExit.load(std::memory_order_acquire);
            });
            if (workCounter.load(std::memory_order_acquire) > 0) {
                workCounter.fetch_sub(1);
            }
        }
    }
}
} // AssetLoad
