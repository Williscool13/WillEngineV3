//
// Created by William on 2026-01-26.
//

#include "audio_load_slot.h"

#include "audio/audio_asset.h"

namespace AssetLoad
{
AudioLoadSlot::AudioLoadSlot() = default;

AudioLoadSlot::~AudioLoadSlot() = default;

void AudioLoadSlot::Initialize(enki::TaskScheduler* _scheduler, Core::InlineFunction<void(bool, AudioSlotHandle)> _notifyCallback)
{
    scheduler = _scheduler;
    notifyCallback = std::move(_notifyCallback);
}

void AudioLoadSlot::Launch(AudioSlotHandle _audioSlotHandle, Audio::WillAudio* _audioEntry)
{
    audioSlotHandle = _audioSlotHandle;
    audioEntry = _audioEntry;

    if (!task.GetIsComplete()) {
        scheduler->WaitforTask(&task);
    }
    task.loadSlot = this;
    scheduler->AddTaskSetToPipe(&task);
}

void AudioLoadSlot::Clear()
{
    audioSlotHandle = AudioSlotHandle::INVALID;
    audioEntry = nullptr;
}

void AudioLoadSlot::LoadAudioTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    loadSlot->audioEntry->mixAudio = MIX_LoadAudio(loadSlot->audioEntry->mixer, loadSlot->audioEntry->source.c_str(), false);
    const bool bSuccess = loadSlot->audioEntry->mixAudio != nullptr;
    if (loadSlot->notifyCallback) {
        loadSlot->notifyCallback(bSuccess, loadSlot->audioSlotHandle);
    }
}
} // Audio
