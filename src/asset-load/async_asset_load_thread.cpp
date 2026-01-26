//
// Created by William on 2026-01-26.
//

#include "async_asset_load_thread.h"

#include <tracy/Tracy.hpp>

#include "asset-load-jobs/audio_load_job.h"
#include "platform/thread_utils.h"

namespace AssetLoad
{
AsyncAssetLoadThread::AsyncAssetLoadThread(enki::TaskScheduler* scheduler)
{
    for (int32_t i = 0; i < AUDIO_JOB_COUNT; ++i) {
        audioLoadSlots[i].Initialize(scheduler, &audioLoadCompleteTransientQueue);
    }

    thisThread = std::jthread([this] { ThreadMain(); });
}

AsyncAssetLoadThread::~AsyncAssetLoadThread()
{
    bShouldExit.store(true, std::memory_order_release);
    workCounter.fetch_add(1);
    wakeCV.notify_one();
}

void AsyncAssetLoadThread::ThreadMain()
{
    ZoneScoped;
    tracy::SetThreadName("AsyncAssetLoadThread");
    Platform::SetThreadName("AsyncAssetLoadThread");

    while (!bShouldExit.load(std::memory_order_acquire)) {
        AudioLoadRequest req;
        if (audioLoadQueue.pop(req)) {
            Core::Handle<AudioLoadSlot> slotHandle = audioLoadAllocator.Add();

            if (slotHandle.IsValid()) {
                AudioLoadSlot& targetSlot = audioLoadSlots[slotHandle.index];
                targetSlot.Launch(slotHandle, req.audioEntry);
            }
        }

        AudioLoadCompleteTransient cqReq;
        if (audioLoadCompleteTransientQueue.pop(cqReq)) {
            AudioLoadSlot& targetSlot = audioLoadSlots[cqReq.loadSlotHandle.index];
            audioLoadCompleteQueue.push({targetSlot.audioEntry, cqReq.success});
            targetSlot.Clear();

            bool res = audioLoadAllocator.Remove(cqReq.loadSlotHandle);
            assert(res);
        }

        if (workCounter.load(std::memory_order_acquire) > 0) {
            workCounter.fetch_sub(1);
        }
        else {
            std::unique_lock lock(wakeMutex);
            wakeCV.wait(lock, [&] {
                return workCounter.load(std::memory_order_acquire) > 0;
            });
            workCounter.fetch_sub(1);
        }
    }
}
} // AssetLoad
