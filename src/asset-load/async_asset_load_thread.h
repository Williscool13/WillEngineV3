//
// Created by William on 2026-01-26.
//

#ifndef WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
#define WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H

#include <atomic>
#include <bitset>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "asset_load_config.h"
#include "asset-load-jobs/audio_load_job.h"
#include "LockFreeQueue/LockFreeQueueCpp11.h"

template<typename T>
using LockFreeQueue = LockFreeQueueCpp11<T>;

namespace AssetLoad
{
struct AudioLoadRequest;
struct AudioLoadComplete;

class AsyncAssetLoadThread
{
public:
    AsyncAssetLoadThread() = delete;

    AsyncAssetLoadThread(enki::TaskScheduler* scheduler);

    ~AsyncAssetLoadThread();

private: // Threading
    void ThreadMain();

private:
    LockFreeQueue<AudioLoadRequest> audioLoadQueue{AUDIO_JOB_QUEUE_COUNT};
    LockFreeQueue<AudioLoadCompleteTransient> audioLoadCompleteTransientQueue{AUDIO_JOB_QUEUE_COUNT};
    LockFreeQueue<AudioLoadComplete> audioLoadCompleteQueue{AUDIO_JOB_QUEUE_COUNT};
    std::array<AudioLoadSlot, AUDIO_JOB_QUEUE_COUNT> audioLoadSlots{};
    Core::HandleAllocator<AudioLoadSlot, AUDIO_JOB_QUEUE_COUNT> audioLoadAllocator{};

private:
    std::jthread thisThread;
    std::atomic<bool> bShouldExit{false};
    std::atomic<uint32_t> workCounter{0};
    std::mutex wakeMutex;
    std::condition_variable wakeCV;
};
} // AssetLoad

#endif //WILL_ENGINE_ASYNC_ASSET_LOAD_THREAD_H
