//
// Created by William on 2026-08-08.
//

#ifndef WILL_ENGINE_MODEL_STORE_H
#define WILL_ENGINE_MODEL_STORE_H

#include <cstdint>

#include "core/containers/virtual_array.h"
#include "core/memory/dirty_bits.h"
#include "core/memory/range_allocator.h"
#include "render/shaders/model_interop.h"

namespace Engine
{
inline constexpr uint32_t MAX_MODEL_SLOTS = 1024 * 1024;

/**
 * Persistent store of model matrices in a stable slot space, independent of the instance slot space.
 * A RangeAllocator hands out contiguous runs (one slot per unique source node for meshes; anything may hold slots with no instance attached).
 * Owners write matrices through SetModel and consumers upload only the slots that changed. Not thread-safe.
 */
class ModelStore
{
public:
    using Range = Core::RangeAllocator::Range;

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::VirtualMemoryManager* vm, Core::AllocTag tag = Core::AllocTag::RenderMesh);

    Range Allocate(uint32_t count);

    void Free(Range& range);

    void SetModel(uint32_t slot, const Model& model)
    {
        models_[slot] = model;
        dirty_.Mark(slot);
    }

    const Model& GetModel(uint32_t slot) const { return models_[slot]; }

    const Model* Models() const { return models_.Data(); }

    template<typename Fn>
    void DrainDirty(uint32_t setIndex, Fn&& emit) { dirty_.Drain(setIndex, GetWatermark(), std::forward<Fn>(emit)); }

    /** Re-emits every live slot on the next drain of every host slot. Debug repair for a mutation that skipped SetModel. */
    void MarkAllDirty() { dirty_.MarkRange(0, GetWatermark()); }

    [[nodiscard]] uint32_t GetWatermark() const { return ranges_.GetWatermark(); }
    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    Core::VirtualArray<Model> models_{};
    Core::RangeAllocator ranges_{};
    Core::DirtyBits dirty_{};
};
} // Engine

#endif //WILL_ENGINE_MODEL_STORE_H
