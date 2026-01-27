//
// Created by William on 2026-01-26.
//

#include "async_asset_load_manager.h"

#include <spdlog/spdlog.h>
#include <tracy/Tracy.hpp>

#include "audio/audio_asset.h"

namespace AssetLoad
{
AsyncAssetLoadManager::AsyncAssetLoadManager(enki::TaskScheduler* scheduler)
    : scheduler(scheduler)
{
    for (uint32_t i = 0; i < AUDIO_JOB_COUNT; ++i) {
        audioLoadSlots[i].Initialize(scheduler, [this](const bool success, const AudioSlotHandle slotHandle) {
            OnAudioLoadComplete(success, slotHandle);
        });
    }
}

AsyncAssetLoadManager::~AsyncAssetLoadManager()
{
    while (GetActiveLoadCount() > 0) {
        ProcessCompletedLoads();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ProcessCompletedLoads();
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

void AsyncAssetLoadManager::ProcessCompletedLoads()
{
    ZoneScoped;

    AudioLoadComplete result;
    uint32_t processedCount = 0;

    while (audioLoadCompleteQueue.try_dequeue(result)) {
        if (result.success && result.audioEntry) {
            SPDLOG_DEBUG("Processed completed audio load: {}", result.audioEntry->source.string());
        }
        else if (result.audioEntry) {
            SPDLOG_WARN("Processed failed audio load: {}", result.audioEntry->source.string());
        }

        ++processedCount;
    }

    if (processedCount > 0) {
        ZoneValue(processedCount);
    }
}
} // AssetLoad
