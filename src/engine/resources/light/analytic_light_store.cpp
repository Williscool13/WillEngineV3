//
// Created by William on 2026-08-08.
//

#include "engine/resources/light/analytic_light_store.h"

#include "engine/logging/engine_log.h"
#include "render/interface/render_interface.h"

namespace Engine
{
void AnalyticLightStore::Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag)
{
    ranges_.Init(capacity, alloc, tag, "AnalyticLightStore");
    lights_ = Core::HeapArray<LightInfo>(alloc, tag, capacity);
    dirty_.Init(capacity, Core::FRAME_BUFFER_COUNT, alloc, tag);
    pendingFrees_ = Core::Vector<PendingFree>(alloc, tag);
}

uint32_t AnalyticLightStore::Allocate()
{
    const Core::RangeAllocator::Range range = ranges_.Allocate(1);
    if (!range.IsValid()) {
        LOG_ERROR(Engine, "Analytic light store full ({} slots)", ranges_.GetCapacity());
        return INVALID_SLOT;
    }
    return range.offset;
}

void AnalyticLightStore::Free(uint32_t& slot)
{
    if (slot == INVALID_SLOT) { return; }
    SetLight(slot, LightInfo{});
    pendingFrees_.PushBack({{slot, 1}, frame_});
    slot = INVALID_SLOT;
}

void AnalyticLightStore::Tick(uint64_t frame)
{
    frame_ = frame;
    while (!pendingFrees_.IsEmpty() && frame_ - pendingFrees_[0].frame >= REUSE_DELAY_FRAMES) {
        ranges_.Free(pendingFrees_[0].range);
        pendingFrees_.RemoveAt(0);
    }
}
} // Engine
