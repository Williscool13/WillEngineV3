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

TriLightStore::Range TriLightStore::AllocateForPrimitive(const StaticModel& model, uint32_t primitiveIndex)
{
    for (const EmissiveTriangleSet& set : model.modelData.emissiveTriangles) {
        if (set.primitiveIndex == primitiveIndex && !set.verts.IsEmpty()) {
            Range range = ranges_.Allocate(static_cast<uint32_t>(set.verts.Size() / 3));
            if (!range.IsValid() && !bWarnedFull_) {
                LOG_WARN(Engine, "Tri light store full; model ({}) primitives get no triangle lights", model.name.c_str());
                bWarnedFull_ = true;
            }
            return range;
        }
    }
    return {};
}

void TriLightStore::Free(Range& range)
{
    if (!range.IsValid()) { return; }
    pendingFrees_.PushBack({range, frame_});
    range = {};
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
