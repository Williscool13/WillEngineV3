//
// Created by William on 2026-07-01.
//

#ifndef WILL_ENGINE_INSTANCE_STORE_H
#define WILL_ENGINE_INSTANCE_STORE_H

#include <cstdint>

#include "core/containers/heap_array.h"
#include "core/memory/range_allocator.h"
#include "core/types/math.h"
#include "engine/core/material_id.h"

namespace Engine
{
class MaterialManager;
struct StaticModel;

/**
 * Persistent per-slot source data one GPU Instance is derived from each frame.
 */
struct InstanceSource
{
    uint32_t primitiveIndex{0};
    int32_t originalMaterialIndex{-1};
    uint32_t sourceNodeIndex{~0u};
    uint32_t modelPrimitiveOrdinal{~0u};
    uint32_t modelSlot{~0u};
    MaterialID materialID{};
    uint64_t blasDeviceAddress{0};
    Mat4 modelSpaceTransform{1.0f};
};

inline constexpr uint32_t MAX_INSTANCE_SLOTS = 256 * 1024;
static_assert(MAX_INSTANCE_SLOTS < (1u << 31));


/**
 * The stable instance slot space: one slot per flattened mesh primitive (static, static-primitive, procedural, spline, text3d, light surfaces). A RangeAllocator hands out one contiguous run per entity; the slot index IS the GPU instance index. Raw Allocate/Free leave material lifetimes to callers; AllocateSingleMeshRange/ReleaseAndFree manage the per-entry material refs. Callers own GPU uploads. Not thread-safe.
 */
class InstanceStore
{
public:
    using Range = Core::RangeAllocator::Range;

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag = Core::AllocTag::RenderMesh);

    Range Allocate(uint32_t count) { return ranges_.Allocate(count); }

    void Free(Range range) { ranges_.Free(range); }

    /** Range over model mesh[0]'s primitives with a uniform material (acquired per entry), identity transforms, and a shared ModelStore slot. Invalid range on an empty model or full store. */
    Range AllocateSingleMeshRange(MaterialManager* materialManager, StaticModel* model, MaterialID material, uint32_t modelSlot);

    /** Releases each entry's material ref, frees the range, and invalidates it. No-op on an invalid range. */
    void ReleaseAndFree(MaterialManager* materialManager, Range& range);

    InstanceSource* Get(Range range) { return range.IsValid() ? &instances_[range.offset] : nullptr; }

    InstanceSource& operator[](uint32_t i) { return instances_[i]; }
    const InstanceSource& operator[](uint32_t i) const { return instances_[i]; }

    InstanceSource* Data() { return instances_.Data(); }
    const InstanceSource* Data() const { return instances_.Data(); }

    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] uint32_t GetWatermark() const { return ranges_.GetWatermark(); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    Core::HeapArray<InstanceSource> instances_{};
    Core::RangeAllocator ranges_{};
};
} // Engine

#endif //WILL_ENGINE_INSTANCE_STORE_H
