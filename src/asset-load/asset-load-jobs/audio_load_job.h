//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_AUDIO_LOAD_JOB_H
#define WILL_ENGINE_AUDIO_LOAD_JOB_H

#include <LockFreeQueue/LockFreeQueueCpp11.h>
#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"


template<typename T>
using LockFreeQueue = LockFreeQueueCpp11<T>;

namespace AssetLoad
{
class AudioLoadSlot;
}

namespace AssetLoad
{
class AudioLoadSlot
{
public:
    AudioLoadSlot();

    ~AudioLoadSlot();

    void Initialize(enki::TaskScheduler* _scheduler, LockFreeQueue<AudioLoadCompleteTransient>* _completeQueue);

    void Launch(AudioSlotHandle _audioSlotHandle, Audio::WillAudio* _audioEntry);

    void Clear();

    Audio::WillAudio* audioEntry{nullptr};

private:
    struct LoadAudioTask : enki::ITaskSet
    {
        AudioLoadSlot* loadSlot{nullptr};

        explicit LoadAudioTask() : ITaskSet(1) {}

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    AudioSlotHandle audioSlotHandle{};

    std::unique_ptr<LoadAudioTask> task{nullptr};
    enki::TaskScheduler* scheduler{nullptr};
    LockFreeQueue<AudioLoadCompleteTransient>* loadCompleteQueue{nullptr};
};
} // Audio

#endif //WILL_ENGINE_AUDIO_LOAD_JOB_H
