//
// Created by William on 2026-01-26.
//

#include "async_asset_load_manager.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "audio/audio_asset.h"
#include "platform/thread_utils.h"
#include "render/pipelines//pipeline_data.h"
#include "render/vulkan/vk_utils.h"

namespace AssetLoad
{
AsyncAssetLoadManager::AsyncAssetLoadManager(enki::TaskScheduler* scheduler, Render::VulkanContext* context, Render::ResourceManager* resourceManager, VkPipelineCache pipelineCache)
    : scheduler(scheduler), context(context), pipelineCache(pipelineCache)
{
    // todo make a new scheduler w/ a smaller pool (and more importantly separate from the main scheduler
    for (uint32_t i = 0; i < AUDIO_JOB_COUNT; ++i) {
        audioLoadSlots[i].Initialize(scheduler, [this](bool success, AudioSlotHandle slotHandle) {
            OnAudioLoadComplete(success, slotHandle);
        });
    }

    for (uint32_t i = 0; i < PIPELINE_JOB_COUNT; ++i) {
        pipelineLoadSlots[i].Initialize(scheduler, context, pipelineCache, [this](bool success, PipelineSlotHandle slotHandle) {
            OnPipelineLoadComplete(success, slotHandle);
        });
    }

    for (uint32_t i = 0; i < MODEL_JOB_COUNT; ++i) {
        // todo fix the queue submit callback that is straight up wrong. Need to add to queue
        modelLoadSlots[i].Initialize(
            scheduler,
            context,
            resourceManager,
            [this](VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal) {
                QueueGPUDispatch(cmd, fence, completionSignal);
            },
            [this](bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle) {
                OnModelLoadComplete(success, modelSlotHandle, uploadStagingSlotHandle);
            }
        );
    }

    for (uint32_t i = 0; i < 8; ++i) {
        uploadStagings[i].Initialize(context, TEXTURE_LOAD_STAGING_SIZE); // todo new staging size
    }

    thisThread = std::jthread([this] { ThreadMain(); });
    gpuDispatchThread = std::jthread([this] { DispatchThreadMain(); });
}

AsyncAssetLoadManager::~AsyncAssetLoadManager() = default;

void AsyncAssetLoadManager::ThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("AsyncAssetLoadMain");
    Platform::SetThreadName("AsyncAssetLoadMain");

    scheduler->RegisterExternalTaskThread();

    while (!bShouldExit.load(std::memory_order_acquire)) {
        {
            ZoneScopedN("Process Audio Requests");
            AudioLoadRequest audioReq{};
            if (audioRequestQueue.try_dequeue(audioReq)) {
                Core::Handle<AudioLoadSlot> slotHandle = audioLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    AudioLoadSlot& slot = audioLoadSlots[slotHandle.index];
                    slot.Launch(slotHandle, audioReq.audioEntry);
                }
            }
        } {
            ZoneScopedN("Process Pipeline Requests");
            PipelineLoadRequest pipelineReq{};
            if (pipelineRequestQueue.try_dequeue(pipelineReq)) {
                Core::Handle<PipelineLoadSlot> slotHandle = pipelineLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    PipelineLoadSlot& slot = pipelineLoadSlots[slotHandle.index];
                    slot.Launch(slotHandle, pipelineReq.entry);
                }
            }
        } {
            ZoneScopedN("Process Model Requests");
            WillModelLoadRequest modelReq{};
            if (modelRequestQueue.try_dequeue(modelReq)) {
                // todo upload staging allocator is lock free (it spins) so this will spin infinitely until a slot frees up, which is not desirable
                Core::Handle<WillModelLoadSlot> slotHandle = modelLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    Core::Handle<UploadStaging> uploadHandle = uploadStagingAllocator.Add();
                    if (uploadHandle.IsValid()) {
                        WillModelLoadSlot& slot = modelLoadSlots[slotHandle.index];
                        UploadStaging* uploadStaging = &uploadStagings[uploadHandle.index];

                        slot.Launch(slotHandle, uploadHandle, uploadStaging, modelReq.model);
                    }
                    else {
                        modelRequestQueue.enqueue(modelReq);
                        // modelLoadAllocator.Remove(slotHandle);
                    }
                }
                else {
                    modelRequestQueue.enqueue(modelReq);
                }
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

void AsyncAssetLoadManager::Join()
{
    bShouldExit.store(true, std::memory_order_release);

    gpuDispatchWorkCounter.fetch_add(1);
    gpuDispatchWakeCV.notify_one();
    gpuDispatchThread.join();

    workCounter.fetch_add(1);
    wakeCV.notify_one();
    thisThread.join();
}

void AsyncAssetLoadManager::DispatchThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("AsyncGPUDispatcherMain");
    Platform::SetThreadName("AsyncGPUDispatcherMain");

    while (!bShouldExit.load(std::memory_order_acquire)) {
        {
            ZoneScopedN("Dispatch GPU Requests");
            constexpr size_t MAX_BATCH = 16;
            dispatchBatch.resize(MAX_BATCH);

            size_t count = gpuDispatchQueue.try_dequeue_bulk(dispatchBatch.begin(), MAX_BATCH);
            if (count > 0) {
                VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submitInfo.commandBufferCount = 1;

                for (size_t i = 0; i < count; ++i) {
                    submitInfo.pCommandBuffers = &dispatchBatch[i].cmd;
                    VK_CHECK(vkQueueSubmit(this->context->transferQueue, 1, &submitInfo, dispatchBatch[i].fence));
                    fences.push_back(dispatchBatch[i].fence);
                }

                VK_CHECK(vkWaitForFences(context->device, count, fences.data(), VK_TRUE, UINT64_MAX));
                for (size_t i = 0; i < count; ++i) {
                    dispatchBatch[i].completionSignal->release();
                }


                dispatchBatch.clear();
                fences.clear();
            }
        }

        if (gpuDispatchWorkCounter.load(std::memory_order_acquire) > 0) {
            gpuDispatchWorkCounter.fetch_sub(1);
        }
        else {
            ZoneScopedN("Idle - Waiting for Work");
            std::unique_lock lock(gpuDispatchWakeMutex);
            gpuDispatchWakeCV.wait(lock, [&] {
                return gpuDispatchWorkCounter.load(std::memory_order_acquire) > 0 || bShouldExit.load(std::memory_order_acquire);
            });
            if (gpuDispatchWorkCounter.load(std::memory_order_acquire) > 0) {
                gpuDispatchWorkCounter.fetch_sub(1);
            }
        }
    }
}

void AsyncAssetLoadManager::RequestAudioLoad(Audio::WillAudio* audioEntry)
{
    ZoneScoped;

    if (!audioEntry) {
        SPDLOG_ERROR("RequestAudioLoad called with null audioEntry");
        return;
    }

    audioRequestQueue.enqueue({audioEntry});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueAudioComplete(AudioLoadComplete& outResult)
{
    return audioLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestPipelineLoad(Render::PipelineData* pipelineData)
{
    ZoneScoped;

    if (!pipelineData) {
        SPDLOG_ERROR("RequestPipelineLoad called with null pipelineData");
        return;
    }

    pipelineRequestQueue.enqueue({pipelineData});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeuePipelineComplete(PipelineLoadComplete& outResult)
{
    return pipelineLoadCompleteQueue.try_dequeue(outResult);
}

void AsyncAssetLoadManager::RequestModelLoad(Render::WillModel* model)
{
    ZoneScoped;

    if (!model) {
        SPDLOG_ERROR("RequestModelLoad called with null model");
        return;
    }

    modelRequestQueue.enqueue({model});
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

bool AsyncAssetLoadManager::TryDequeueModelComplete(WillModelLoadComplete& outResult)
{
    return modelLoadCompleteQueue.try_dequeue(outResult);
}


void AsyncAssetLoadManager::QueueGPUDispatch(VkCommandBuffer cmd, VkFence fence, std::binary_semaphore* completionSignal)
{
    gpuDispatchQueue.enqueue({cmd, fence, completionSignal});
    gpuDispatchWorkCounter.fetch_add(1);
    gpuDispatchWakeCV.notify_one();
}

void AsyncAssetLoadManager::OnAudioLoadComplete(bool success, AudioSlotHandle slotHandle)
{
    ZoneScoped;

    if (!audioLoadAllocator.IsValid(slotHandle)) {
        SPDLOG_ERROR("OnAudioLoadComplete called with invalid slot handle");
        return;
    }

    AudioLoadSlot& slot = audioLoadSlots[slotHandle.index];
    audioLoadCompleteQueue.enqueue({slot.audioEntry, success});

    if (success) {
        SPDLOG_INFO("Finished loading audio file: {}", slot.audioEntry->source.string());
    }
    else {
        SPDLOG_ERROR("Failed to load audio file: {}", slot.audioEntry->source.string());
    }

    slot.Clear();
    bool removed = audioLoadAllocator.Remove(slotHandle);
    assert(removed && "Failed to remove valid slot handle");

    // Slot has freed up, need thread to check for new work
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnPipelineLoadComplete(bool success, PipelineSlotHandle slotHandle)
{
    ZoneScoped;

    if (!pipelineLoadAllocator.IsValid(slotHandle)) {
        SPDLOG_ERROR("OnPipelineLoadComplete called with invalid slot handle");
        return;
    }

    PipelineLoadSlot& slot = pipelineLoadSlots[slotHandle.index];
    pipelineLoadCompleteQueue.enqueue({slot.pipelineData, success});

    if (!success) {
        SPDLOG_WARN("Failed to load pipeline");
    }

    slot.Clear();
    bool removed = pipelineLoadAllocator.Remove(slotHandle);
    assert(removed && "Failed to remove valid slot handle");

    // Slot has freed up, need thread to check for new work
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadManager::OnModelLoadComplete(bool success, ModelSlotHandle modelSlotHandle, UploadStagingSlotHandle uploadStagingSlotHandle)
{
    ZoneScoped;

    if (!modelLoadAllocator.IsValid(modelSlotHandle)) {
        SPDLOG_ERROR("OnModelLoadComplete called with invalid slot handle");
        return;
    }

    WillModelLoadSlot& slot = modelLoadSlots[modelSlotHandle.index];
    modelLoadCompleteQueue.enqueue({slot.outputModel, success});

    if (!success) {
        SPDLOG_ERROR("Failed to load model: {}", slot.outputModel->name);
    }

    slot.Clear();
    modelLoadAllocator.Remove(modelSlotHandle);

    if (uploadStagingAllocator.IsValid(uploadStagingSlotHandle)) {
        uploadStagingAllocator.Remove(uploadStagingSlotHandle);
    }

    // Slot has freed up, need thread to check for new work
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}
} // AssetLoad
