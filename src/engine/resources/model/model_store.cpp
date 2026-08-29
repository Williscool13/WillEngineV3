//
// Created by William on 2026-08-08.
//

#include "engine/resources/model/model_store.h"

#include "engine/logging/engine_log.h"
#include "render/interface/render_interface.h"

namespace Engine
{
void ModelStore::Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::VirtualMemoryManager* vm, Core::AllocTag tag)
{
    models_ = Core::VirtualArray<Model>(vm, tag, capacity, "ModelStore");
    ranges_.Init(capacity, alloc, tag, "ModelStore");
    dirty_.Init(capacity, Core::FRAME_BUFFER_COUNT, alloc, tag);
}

ModelStore::Range ModelStore::Allocate(uint32_t count)
{
    const Range range = ranges_.Allocate(count);
    if (range.IsValid()) {
        models_.EnsureCommitted(ranges_.GetWatermark());
        for (uint32_t i = 0; i < range.count; ++i) {
            SetModel(range.offset + i, Model{});
        }
    }
    else if (count > 0) {
        LOG_ERROR(Engine, "Model store full; cannot allocate {} model slots", count);
    }
    return range;
}

void ModelStore::Free(Range& range)
{
    if (!range.IsValid()) { return; }
    for (uint32_t i = 0; i < range.count; ++i) {
        SetModel(range.offset + i, Model{});
    }
    ranges_.Free(range);
    models_.Trim(ranges_.GetWatermark());
    range = {};
}
} // Engine
