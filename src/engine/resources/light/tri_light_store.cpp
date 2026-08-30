//
// Created by William on 2026-08-08.
//

#include "engine/resources/light/tri_light_store.h"

#include "engine/logging/engine_log.h"
#include "engine/resources/model/static_model.h"

namespace Engine
{
void TriLightStore::Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag)
{
    ranges_.Init(capacity, alloc, tag, "TriLightStore");
    pendingFrees_ = Core::Vector<PendingFree>(alloc, tag);
}

TriLightStore::Range TriLightStore::Reserve(uint32_t instanceSlot, uint32_t triangleCount, const char* ownerName)
{
    if (triangleCount == 0) { return {}; }

    if (reservations_.IsFull()) {
        if (!bWarnedCapReached_) {
            bWarnedCapReached_ = true;
            LOG_WARN(Engine, "Emissive instance cap ({}) reached; further emissive primitives will not light, starting with model ({})", MAX_EMISSIVE_GROUPS, ownerName);
        }
        return {};
    }

    const Range range = ranges_.Allocate(triangleCount);
    if (!range.IsValid()) {
        if (!bWarnedFull_) {
            bWarnedFull_ = true;
            LOG_WARN(Engine, "Tri light store full; further emissive primitives get no triangle lights");
        }
        return {};
    }

    reservations_.PushBack({instanceSlot, range});
    return range;
}

void TriLightStore::Release(uint32_t instanceSlot)
{
    for (size_t i = 0; i < reservations_.Size(); ++i) {
        if (reservations_[i].instanceSlot != instanceSlot) { continue; }
        pendingFrees_.PushBack({reservations_[i].range, frame_});
        reservations_.SwapRemove(i);
        bWarnedCapReached_ = false;
        return;
    }
}

void TriLightStore::Tick(uint64_t frame)
{
    frame_ = frame;
    while (!pendingFrees_.IsEmpty() && frame_ - pendingFrees_[0].frame >= REUSE_DELAY_FRAMES) {
        ranges_.Free(pendingFrees_[0].range);
        pendingFrees_.RemoveAt(0);
        bWarnedFull_ = false;
    }
}
} // Engine
