//
// Created by William on 2025-12-17.
//

#include "asset_load_thread.h"

#include <enkiTS/src/TaskScheduler.h>
#include <spdlog/spdlog.h>

#include "asset-load-jobs/pipeline_load_job.h"
#include "asset-load-jobs/texture_load_job.h"
#include "asset-load-jobs/will_model_load_job.h"
#include "platform/paths.h"
#include "platform/thread_utils.h"
#include "render/texture_asset.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/vulkan/vk_context.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_utils.h"
#include "tracy/Tracy.hpp"

namespace AssetLoad
{
AssetLoadThread::AssetLoadThread() = default;

AssetLoadThread::AssetLoadThread(enki::TaskScheduler* scheduler, Render::VulkanContext* context, Render::ResourceManager* resourceManager, Render::PipelineManager* pipelineManager)
    : context(context), resourceManager(resourceManager), scheduler(scheduler)
{
    VkCommandPoolCreateInfo poolInfo = Render::VkHelpers::CommandPoolCreateInfo(context->transferQueueFamily);
    VK_CHECK(vkCreateCommandPool(context->device, &poolInfo, nullptr, &commandPool));

    constexpr uint32_t totalCommandBuffers = WILL_MODEL_JOB_COUNT + TEXTURE_JOB_COUNT + 1;
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

    pipelineJobs.reserve(PIPELINE_JOB_COUNT);
    for (int32_t i = 0; i < PIPELINE_JOB_COUNT; ++i) {
        pipelineJobs.emplace_back(std::make_unique<PipelineLoadJob>(context, resourceManager, pipelineManager->GetPipelineCache()));
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

void AssetLoadThread::RequestPipelineLoad(const std::string& name, Render::PipelineData* data)
{
    pipelineLoadQueue.push({name, data});
}

bool AssetLoadThread::ResolvePipelineLoads(PipelineComplete& pipelineComplete)
{
    return pipelineCompleteLoadQueue.pop(pipelineComplete);
}

Render::Sampler AssetLoadThread::CreateSampler(const VkSamplerCreateInfo& samplerCreateInfo) const
{
    return Render::Sampler::CreateSampler(context, samplerCreateInfo);
}

void AssetLoadThread::ThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("AssetLoadThread");
    Platform::SetThreadName("AssetLoadThread");

    while (!bShouldExit.load(std::memory_order_acquire)) {
        ZoneScopedN("AssetLoadLoop");
        bool didWork = false;

        // Model loading jobs
        {
            ZoneScopedN("ModelJobDispatch");
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


                assetLoadSlots[slotIdx].name = loadRequest.model->name;
                assetLoadSlots[slotIdx].job = job;
                assetLoadSlots[slotIdx].loadState = AssetLoadState::Idle;
                assetLoadSlots[slotIdx].type = AssetType::WillModel;
                assetLoadSlots[slotIdx].startTime = std::chrono::steady_clock::now();
                assetLoadSlots[slotIdx].uploadCount = 0;
                activeSlotMask[slotIdx] = true;
            }
        }

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

                assetLoadSlots[slotIdx].name = loadRequest.texture->name;
                assetLoadSlots[slotIdx].job = job;
                assetLoadSlots[slotIdx].loadState = AssetLoadState::Idle;
                assetLoadSlots[slotIdx].type = AssetType::Texture;
                assetLoadSlots[slotIdx].startTime = std::chrono::steady_clock::now();
                assetLoadSlots[slotIdx].uploadCount = 0;
                activeSlotMask[slotIdx] = true;
            }
        }

        // Pipeline loading jobs
        {
            ZoneScopedN("PipelineJobDispatch");
            size_t freePipelineJobCount = 0;
            for (size_t i = 0; i < pipelineJobActive.size(); ++i) {
                if (!pipelineJobActive[i]) {
                    freePipelineJobCount++;
                }
            }

            for (size_t jobsStarted = 0; jobsStarted < freePipelineJobCount; ++jobsStarted) {
                PipelineLoadRequest loadRequest{};
                if (!pipelineLoadQueue.pop(loadRequest)) {
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
                for (size_t i = 0; i < pipelineJobActive.size(); ++i) {
                    if (!pipelineJobActive[i]) {
                        freeJobIdx = i;
                        break;
                    }
                }

                PipelineLoadJob* job = pipelineJobs[freeJobIdx].get();
                job->outputDate = loadRequest.entry;
                pipelineJobActive[freeJobIdx] = true;

                assetLoadSlots[slotIdx].name = loadRequest.name;
                assetLoadSlots[slotIdx].job = job;
                assetLoadSlots[slotIdx].loadState = AssetLoadState::Idle;
                assetLoadSlots[slotIdx].type = AssetType::Pipeline;
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
                        case AssetType::WillModel:
                        {
                            ZoneScopedN("CompleteWillModel");
                            auto* modelJob = dynamic_cast<WillModelLoadJob*>(job);
                            //
                            {
                                ZoneScopedN("PushCompleteQueue");
                                modelCompleteLoadQueue.push({modelJob->willModelHandle, modelJob->outputModel, success});
                            }

                            //
                            {
                                ZoneScopedN("FindAndResetJob");

                                for (size_t i = 0; i < willModelJobs.size(); ++i) {
                                    if (willModelJobs[i].get() == job) {
                                        job->Reset();
                                        willModelJobActive[i] = false;
                                        break;
                                    }
                                }
                            }

                            if (success) {
                                SPDLOG_INFO("'{}' willmodel loaded in {}ms with {} uploads", slot.name, durationMs, slot.uploadCount);
                            }
                            else {
                                SPDLOG_INFO("'{}' willmodel failed to load in {}ms with {} uploads", slot.name, durationMs, slot.uploadCount);
                            }
                            break;
                        }
                        case AssetType::Texture:
                        {
                            ZoneScopedN("CompleteTexture");
                            auto* textureJob = dynamic_cast<TextureLoadJob*>(job);
                            //
                            {
                                ZoneScopedN("PushCompleteQueue");
                                textureCompleteLoadQueue.push({textureJob->textureHandle, textureJob->outputTexture, success});
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
                        case AssetType::Pipeline:
                        {
                            ZoneScopedN("CompletePipeline");
                            auto* pipelineJob = dynamic_cast<PipelineLoadJob*>(job);
                            //
                            {
                                ZoneScopedN("PushCompleteQueue");
                                pipelineCompleteLoadQueue.push({slot.name, pipelineJob->outputDate, success});
                            }
                            //
                            {
                                ZoneScopedN("FindAndResetJob");
                                for (size_t i = 0; i < pipelineJobs.size(); ++i) {
                                    if (pipelineJobs[i].get() == job) {
                                        job->Reset();
                                        pipelineJobActive[i] = false;
                                        break;
                                    }
                                }
                            }

                            if (success) {
                                SPDLOG_INFO("'{}' pipeline loaded in {}ms", slot.name, durationMs);
                            }
                            else {
                                SPDLOG_INFO("'{}' pipeline failed to load in {}ms", slot.name, durationMs);
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
        }


        if (!didWork) {
            ZoneScopedN("IdleSleep");
            std::chrono::microseconds idleWait = std::chrono::microseconds(10);
            std::this_thread::sleep_for(idleWait);
        }
    }
}
} // AssetLoad
