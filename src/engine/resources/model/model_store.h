//
// Created by William on 2026-08-08.
//

#ifndef WILL_ENGINE_MODEL_STORE_H
#define WILL_ENGINE_MODEL_STORE_H

#include <cstdint>

#include "core/containers/heap_array.h"
#include "core/memory/range_allocator.h"
#include "render/shaders/model_interop.h"

namespace Engine
{
inline constexpr uint32_t MAX_MODEL_SLOTS = 64 * 1024;

/**
 * Persistent store of model matrices in a stable slot space, independent of the instance slot space. A RangeAllocator hands out contiguous runs (one slot per unique source node for meshes; anything may hold slots with no instance attached). Owners write matrices in place on dirty; consumers snapshot [0, watermark). Not thread-safe.
 */
class ModelStore
{
public:
    using Range = Core::RangeAllocator::Range;

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag = Core::AllocTag::RenderMesh);

    /** Invalid range (and an error log) when full. */
    Range Allocate(uint32_t count);

    /** Frees and invalidates. No-op on an invalid range. */
    void Free(Range& range);

    Model* Models() { return models_.Data(); }
    const Model* Models() const { return models_.Data(); }

    [[nodiscard]] uint32_t GetWatermark() const { return ranges_.GetWatermark(); }
    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    Core::HeapArray<Model> models_{};
    Core::RangeAllocator ranges_{};
};
} // Engine

#endif //WILL_ENGINE_MODEL_STORE_H
