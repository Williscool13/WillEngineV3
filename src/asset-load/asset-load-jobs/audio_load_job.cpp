//
// Created by William on 2026-01-26.
//

#include "audio_load_job.h"

#include "audio/audio_asset.h"

namespace AssetLoad
{
AudioLoadSlot::AudioLoadSlot() = default;

AudioLoadSlot::~AudioLoadSlot() = default;

void AudioLoadSlot::Initialize(enki::TaskScheduler* _scheduler, std::function<void(bool success, AudioSlotHandle slotHandle)> _notifyCallback)
{

    scheduler = _scheduler;
    notifyCallback = _notifyCallback;
    task = std::make_unique<LoadAudioTask>();
}

void AudioLoadSlot::Launch(AudioSlotHandle _audioSlotHandle, Audio::WillAudio* _audioEntry)
{
    audioSlotHandle = _audioSlotHandle;
    audioEntry = _audioEntry;

    task->loadSlot = this;
    scheduler->AddTaskSetToPipe(task.get());
}

void AudioLoadSlot::Clear()
{
    audioSlotHandle = AudioSlotHandle::INVALID;
    audioEntry = nullptr;
}

void AudioLoadSlot::LoadAudioTask::ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum)
{
    loadSlot->audioEntry->mixAudio = MIX_LoadAudio(loadSlot->audioEntry->mixer, loadSlot->audioEntry->source.string().c_str(), false);
    const bool bSuccess = loadSlot->audioEntry->mixAudio != nullptr;
    if (loadSlot->notifyCallback) {
        loadSlot->notifyCallback(bSuccess, loadSlot->audioSlotHandle);
    }
}
} // Audio
