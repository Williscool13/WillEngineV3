//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
#define WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H

#include <atomic>
#include <array>

#include <TaskScheduler.h>
#include <concurrentqueue/concurrentqueue.h>

#include "asset-load-jobs/audio_load_job.h"
#include "core/allocators/lock_free_handle_allocator.h"

namespace AssetLoad
{
struct AudioLoadComplete
{
    Audio::WillAudio* audioEntry;
    bool success;
};

class AsyncAssetLoadManager
{
public:
    static constexpr uint32_t AUDIO_JOB_COUNT = 256;

    explicit AsyncAssetLoadManager(enki::TaskScheduler* scheduler);
    ~AsyncAssetLoadManager();

    void RequestAudioLoad(Audio::WillAudio* audioEntry);

    void ProcessCompletedLoads();

    [[nodiscard]] uint32_t GetActiveLoadCount() const {
        return audioLoadAllocator.GetCount();
    }

private:
    Core::LockFreeHandleAllocator<AudioLoadSlot, AUDIO_JOB_COUNT> audioLoadAllocator;
    std::array<AudioLoadSlot, AUDIO_JOB_COUNT> audioLoadSlots;
    moodycamel::ConcurrentQueue<AudioLoadComplete> audioLoadCompleteQueue;
    enki::TaskScheduler* scheduler;

    void OnAudioLoadComplete(bool success, AudioSlotHandle slotHandle);
};

} // AssetLoad
#endif //WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
