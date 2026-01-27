//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
#define WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H

#include <atomic>
#include <array>

#include <TaskScheduler.h>
#include <concurrentqueue/concurrentqueue.h>

#include "asset-load-jobs/audio_load_slot.h"
#include "asset-load-jobs/pipeline_load_slot.h"
#include "core/allocators/lock_free_handle_allocator.h"

namespace AssetLoad
{
struct AudioLoadRequest
{
    Audio::WillAudio* audioEntry;
};

struct AudioLoadComplete
{
    Audio::WillAudio* audioEntry;
    bool success;
};

struct PipelineLoadRequest
{
    Render::PipelineData* entry;
};

struct PipelineLoadComplete
{
    Render::PipelineData* pipelineData;
    bool success;
};


class AsyncAssetLoadManager
{
public:
    static constexpr uint32_t AUDIO_JOB_COUNT = 256;

    AsyncAssetLoadManager(enki::TaskScheduler* scheduler,
                          Render::VulkanContext* context,
                          VkPipelineCache pipelineCache);

    ~AsyncAssetLoadManager();

    // Audio Loading
    void RequestAudioLoad(Audio::WillAudio* audioEntry);
    bool TryDequeueAudioComplete(AudioLoadComplete& outResult);

    // Pipeline loading
    void RequestPipelineLoad(Render::PipelineData* pipelineData);
    bool TryDequeuePipelineComplete(PipelineLoadComplete& outResult);

    [[nodiscard]] uint32_t GetActiveAudioLoadCount() const
    {
        return audioLoadAllocator.GetCount();
    }

    [[nodiscard]] uint32_t GetActivePipelineLoadCount() const
    {
        return pipelineLoadAllocator.GetCount();
    }

private:
    // Audio loading
    moodycamel::ConcurrentQueue<AudioLoadRequest> pipelineRequestQueue;
    Core::HandleAllocator<AudioLoadSlot, AUDIO_JOB_COUNT> audioLoadAllocator;
    std::array<AudioLoadSlot, AUDIO_JOB_COUNT> audioLoadSlots;
    moodycamel::ConcurrentQueue<AudioLoadComplete> audioLoadCompleteQueue;

    // Pipeline loading
    Core::HandleAllocator<PipelineLoadSlot, PIPELINE_JOB_COUNT> pipelineLoadAllocator;
    std::array<PipelineLoadSlot, PIPELINE_JOB_COUNT> pipelineLoadSlots;
    moodycamel::ConcurrentQueue<PipelineLoadComplete> pipelineLoadCompleteQueue;

    enki::TaskScheduler* scheduler;
    Render::VulkanContext* context;
    VkPipelineCache pipelineCache;

    void OnAudioLoadComplete(bool success, AudioSlotHandle slotHandle);
    void OnPipelineLoadComplete(bool success, PipelineSlotHandle slotHandle);
};
} // AssetLoad
#endif //WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
