//
// Created by William on 2026-01-26.
//

#include "async_asset_load_manager.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "audio/audio_asset.h"
#include "platform/thread_utils.h"
#include "render/pipelines//pipeline_data.h"

namespace AssetLoad
{
AsyncAssetLoadManager::AsyncAssetLoadManager(enki::TaskScheduler* scheduler, Render::VulkanContext* context, VkPipelineCache pipelineCache)
    : scheduler(scheduler), context(context), pipelineCache(pipelineCache)
{
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
    thisThread = std::jthread([this] { ThreadMain(); });
}

AsyncAssetLoadManager::~AsyncAssetLoadManager()
{
    bShouldExit.store(true, std::memory_order_release);
    workCounter.fetch_add(1);
    wakeCV.notify_one();

    while (GetActiveAudioLoadCount() > 0 || GetActivePipelineLoadCount() > 0) {
        AudioLoadComplete audioResult;
        while (TryDequeueAudioComplete(audioResult)) {}

        PipelineLoadComplete pipelineResult;
        while (TryDequeuePipelineComplete(pipelineResult)) {}

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    AudioLoadComplete audioResult;
    while (TryDequeueAudioComplete(audioResult)) {}

    PipelineLoadComplete pipelineResult;
    while (TryDequeuePipelineComplete(pipelineResult)) {}
}

void AsyncAssetLoadManager::ThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("AsyncAssetLoadThread");
    Platform::SetThreadName("AsyncAssetLoadThread");

    scheduler->RegisterExternalTaskThread();

    while (!bShouldExit.load(std::memory_order_acquire)) {
        {
            ZoneScopedN("Process Audio Requests");
            AudioLoadRequest audioReq;
            if (audioRequestQueue.try_dequeue(audioReq)) {
                Core::Handle<AudioLoadSlot> slotHandle = audioLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    AudioLoadSlot& slot = audioLoadSlots[slotHandle.index];
                    slot.Launch(slotHandle, audioReq.audioEntry);
                }
            }
        }

        {
            ZoneScopedN("Process Pipeline Requests");
            PipelineLoadRequest pipelineReq;
            if (pipelineRequestQueue.try_dequeue(pipelineReq)) {
                Core::Handle<PipelineLoadSlot> slotHandle = pipelineLoadAllocator.Add();
                if (slotHandle.IsValid()) {
                    PipelineLoadSlot& slot = pipelineLoadSlots[slotHandle.index];
                    slot.Launch(slotHandle, pipelineReq.entry);
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
}
} // AssetLoad
