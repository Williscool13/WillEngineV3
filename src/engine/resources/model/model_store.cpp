//
// Created by William on 2026-08-08.
//

#include "engine/resources/model/model_store.h"

#include "engine/logging/engine_log.h"

namespace Engine
{
void ModelStore::Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag)
{
    models_ = Core::HeapArray<Model>(alloc, tag, capacity);
    ranges_.Init(capacity, alloc, tag, "ModelStore");
}

ModelStore::Range ModelStore::Allocate(uint32_t count)
{
    const Range range = ranges_.Allocate(count);
    if (!range.IsValid() && count > 0) {
        LOG_ERROR(Engine, "Model store full; cannot allocate {} model slots", count);
    }
    return range;
}

void ModelStore::Free(Range& range)
{
    if (!range.IsValid()) { return; }
    ranges_.Free(range);
    range = {};
}
} // Engine
