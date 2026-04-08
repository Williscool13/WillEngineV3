//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_AUDIO_LOAD_JOB_H
#define WILL_ENGINE_AUDIO_LOAD_JOB_H

#include <TaskScheduler.h>

#include "asset-load/asset_load_types.h"
#include "core/containers/inline_function.h"

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

    void Initialize(enki::TaskScheduler* _scheduler, Core::InlineFunction<void(bool, AudioSlotHandle)> _notifyCallback);

    void Launch(AudioSlotHandle _audioSlotHandle, Audio::WillAudio* _audioEntry);

    void Clear();

    Audio::WillAudio* audioEntry{nullptr};

private:
    struct LoadAudioTask : enki::ITaskSet
    {
        AudioLoadSlot* loadSlot{nullptr};

        explicit LoadAudioTask() : ITaskSet(1)
        {
            m_Priority = enki::TASK_PRIORITY_LOW;
        }

        void ExecuteRange(enki::TaskSetPartition range, uint32_t threadNum) override;
    };

    AudioSlotHandle audioSlotHandle{};

    LoadAudioTask task{};
    enki::TaskScheduler* scheduler{nullptr};
    Core::InlineFunction<void(bool, AudioSlotHandle)> notifyCallback;
};
} // Audio

#endif //WILL_ENGINE_AUDIO_LOAD_JOB_H
