//
// Created by William on 2026-07-01.
//

#ifndef WILL_ENGINE_INSTANCE_STORE_H
#define WILL_ENGINE_INSTANCE_STORE_H

#include <cstdint>

#include "core/containers/virtual_array.h"
#include "core/memory/dirty_bits.h"
#include "core/memory/range_allocator.h"
#include "core/types/math.h"
#include "engine/core/material_id.h"
#include "render/shaders/flags_interop.h"
#include "render/shaders/model_interop.h"

namespace Engine
{
class MaterialManager;
class TriLightStore;
struct StaticModel;
struct PrimitiveProperty;

/**
 * Persistent per-slot source data one GPU Instance is derived from each frame.
 */
struct InstanceSource
{
    uint32_t primitiveIndex{0};
    uint32_t sourceNodeIndex{~0u};
    uint32_t modelPrimitiveOrdinal{~0u};
    uint32_t modelSlot{~0u};
    int32_t materialSlot{-1};
    uint32_t materialIndex{~0u};
    MaterialID materialID{};
    uint64_t blasDeviceAddress{0};
    Mat4 modelSpaceTransform{1.0f};
    uint64_t stableId{0};
    uint32_t lightIndex{~0u};
    uint32_t flags{INSTANCE_FLAG_MOTION_BLUR | INSTANCE_FLAG_ALPHA_CUTOUT | INSTANCE_FLAG_DDGI_VISIBLE};
    bool bVisible{true};

    /**
     * TriLightStore range covering this primitive's full emissive triangle set.
     */
    Core::RangeAllocator::Range triLightRange{};
};

/**
 * Caller-varying fields for FillEntry; everything else is derived from the primitive and the managers.
 */
struct InstanceFill
{
    MaterialID material{};
    uint32_t modelSlot{~0u};
    int32_t materialSlot{-1};
    uint32_t sourceNodeIndex{0};
    uint32_t modelPrimitiveOrdinal{~0u};
    Mat4 modelSpaceTransform{1.0f};
    bool bEmissiveLight{false};
};


/**
 * The stable instance slot space: one slot per flattened mesh primitive (static, static-primitive, procedural, spline, text3d, light surfaces). A RangeAllocator hands out one contiguous run per entity; the slot index IS the GPU instance index. Raw Allocate/Free leave material lifetimes to callers; AllocateSingleMeshRange/ReleaseAndFree manage the per-entry material refs. Callers own GPU uploads. Not thread-safe.
 * InstanceSource is the authority; the GPU Instance array is a projection of it rewritten on every mutation, dead while the slot is not visible. Nothing hands out a mutable InstanceSource.
 */
class InstanceStore
{
public:
    using Range = Core::RangeAllocator::Range;
    static constexpr Instance DEAD_INSTANCE{.primitiveIndex = DEAD_SLOT_PRIMITIVE_INDEX};

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::VirtualMemoryManager* vm, Core::AllocTag tag = Core::AllocTag::RenderMesh);

    Range Allocate(uint32_t count);

    Range AllocateSingleMeshRange(MaterialManager* materialManager, TriLightStore* triLightStore, StaticModel* model, MaterialID material, uint32_t modelSlot, bool bEmissiveLight);

    void ReleaseAndFree(MaterialManager* materialManager, TriLightStore* triLightStore, Range& range);

    // Writes
    void FillEntry(uint32_t slot, MaterialManager* materialManager, TriLightStore* triLightStore, StaticModel* model, const PrimitiveProperty& primitive, const InstanceFill& fill);

    void SetMaterial(uint32_t slot, MaterialManager* materialManager, MaterialID material);

    void SetLightIndex(uint32_t slot, uint32_t lightIndex);

    void SetRenderState(Range range, bool bVisible, uint32_t flags, uint64_t stableId);

    const InstanceSource& operator[](uint32_t i) const { return instances_[i]; }

    const Instance* Instances() const { return gpuInstances_.Data(); }

    template<typename Fn>
    void DrainDirty(uint32_t setIndex, Fn&& emit) { dirty_.Drain(setIndex, GetWatermark(), std::forward<Fn>(emit)); }

    void MarkAllDirty() { dirty_.MarkRange(0, GetWatermark()); }

    [[nodiscard]] uint32_t VerifyRecords() const;

    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] uint32_t GetWatermark() const { return ranges_.GetWatermark(); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    /** Private because it returns the slots without releasing their materials or tri-light runs. */
    void Free(Range range);

    [[nodiscard]] Instance MakeRecord(uint32_t slot) const;

    void WriteRecord(uint32_t slot);

    Core::VirtualArray<InstanceSource> instances_{};
    Core::VirtualArray<Instance> gpuInstances_{};
    Core::RangeAllocator ranges_{};
    Core::DirtyBits dirty_{};
};
} // Engine

#endif //WILL_ENGINE_INSTANCE_STORE_H
