//
// Created by William on 2026-01-26.
//

#include "async_asset_load_manager.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "audio/audio_asset.h"
#include "render/pipelines//pipeline_data.h"

namespace AssetLoad
{
AsyncAssetLoadManager::AsyncAssetLoadManager(enki::TaskScheduler* scheduler,
                                             Render::VulkanContext* context,
                                             VkPipelineCache pipelineCache)
    : scheduler(scheduler)
      , context(context)
      , pipelineCache(pipelineCache)
{
    // Initialize audio slots
    for (uint32_t i = 0; i < AUDIO_JOB_COUNT; ++i) {
        audioLoadSlots[i].Initialize(scheduler, [this](bool success, AudioSlotHandle slotHandle) {
            OnAudioLoadComplete(success, slotHandle);
        });
    }

    // Initialize pipeline slots
    for (uint32_t i = 0; i < PIPELINE_JOB_COUNT; ++i) {
        pipelineLoadSlots[i].Initialize(scheduler, context, pipelineCache, [this](bool success, PipelineSlotHandle slotHandle) {
            OnPipelineLoadComplete(success, slotHandle);
        });
    }
}

AsyncAssetLoadManager::~AsyncAssetLoadManager()
{
    while (GetActiveAudioLoadCount() > 0 || GetActivePipelineLoadCount() > 0) {
        AudioLoadComplete audioResult;
        while (TryDequeueAudioComplete(audioResult)) {}

        PipelineLoadComplete pipelineResult;
        while (TryDequeuePipelineComplete(pipelineResult)) {}
    }

    AudioLoadComplete audioResult;
    while (TryDequeueAudioComplete(audioResult)) {}

    PipelineLoadComplete pipelineResult;
    while (TryDequeuePipelineComplete(pipelineResult)) {}
}

void AsyncAssetLoadManager::RequestAudioLoad(Audio::WillAudio* audioEntry)
{
    ZoneScoped;

    if (!audioEntry) {
        SPDLOG_ERROR("RequestAudioLoad called with null audioEntry");
        return;
    }

    Core::Handle<AudioLoadSlot> slotHandle = audioLoadAllocator.Add();
    if (!slotHandle.IsValid()) {
        SPDLOG_WARN("Audio load slots full ({}), dropping request for: {}", AUDIO_JOB_COUNT, audioEntry->source.string());
        return;
    }

    AudioLoadSlot& slot = audioLoadSlots[slotHandle.index];
    slot.Launch(slotHandle, audioEntry);

    SPDLOG_DEBUG("Started loading audio file: {} (slot: {})", audioEntry->source.string(), slotHandle.index);
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

    Core::Handle<PipelineLoadSlot> slotHandle = pipelineLoadAllocator.Add();
    if (!slotHandle.IsValid()) {
        SPDLOG_WARN("Pipeline load slots full ({}), dropping request", PIPELINE_JOB_COUNT);
        return;
    }

    PipelineLoadSlot& slot = pipelineLoadSlots[slotHandle.index];
    slot.Launch(slotHandle, pipelineData);

    SPDLOG_DEBUG("Started loading pipeline (slot: {})", slotHandle.index);
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
