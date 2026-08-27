//
// Created by William on 2026-07-01.
//

#ifndef WILL_ENGINE_INSTANCE_STORE_H
#define WILL_ENGINE_INSTANCE_STORE_H

#include <cstdint>

#include "core/containers/virtual_array.h"
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
    int32_t originalMaterialIndex{-1};
    uint32_t sourceNodeIndex{~0u};
    uint32_t modelPrimitiveOrdinal{~0u};
    uint32_t modelSlot{~0u};
    uint32_t materialIndex{~0u};
    MaterialID materialID{};
    uint64_t blasDeviceAddress{0};
    Mat4 modelSpaceTransform{1.0f};

    /**
     * TriLightStore range covering this primitive's full emissive triangle set
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
    int32_t originalMaterialIndex{-1};
    uint32_t sourceNodeIndex{0};
    uint32_t modelPrimitiveOrdinal{~0u};
    Mat4 modelSpaceTransform{1.0f};
};


/**
 * The stable instance slot space: one slot per flattened mesh primitive (static, static-primitive, procedural, spline, text3d, light surfaces). A RangeAllocator hands out one contiguous run per entity; the slot index IS the GPU instance index. Raw Allocate/Free leave material lifetimes to callers; AllocateSingleMeshRange/ReleaseAndFree manage the per-entry material refs. Callers own GPU uploads. Not thread-safe.
 * InstanceSource and the GPU Instance record are parallel slot arrays written together, so no mutable InstanceSource is handed out.
 */
class InstanceStore
{
public:
    using Range = Core::RangeAllocator::Range;

    /** Unallocated, freed and hidden slots. */
    static constexpr Instance DEAD_INSTANCE{.primitiveIndex = DEAD_SLOT_PRIMITIVE_INDEX};

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::VirtualMemoryManager* vm, Core::AllocTag tag = Core::AllocTag::RenderMesh);

    Range Allocate(uint32_t count);

    void Free(Range range);

    /** Range over model mesh[0]'s primitives with a uniform material (acquired per entry), identity transforms, a shared ModelStore slot, and a tri-light range per emissive primitive. Invalid range on an empty model or full store. Null triLightStore suppresses tri-light allocation (light proxy surfaces resolve BRDF hits via their analytic light). */
    Range AllocateSingleMeshRange(MaterialManager* materialManager, TriLightStore* triLightStore, StaticModel* model, MaterialID material, uint32_t modelSlot);

    /** The single writer for InstanceSource entries: acquires the material, stamps the stable material index, and allocates the primitive's tri-light range (skipped when triLightStore is null). */
    void FillEntry(uint32_t slot, MaterialManager* materialManager, TriLightStore* triLightStore, StaticModel* model, const PrimitiveProperty& primitive, const InstanceFill& fill);

    /** Releases each entry's material ref and tri-light range, frees the range, and invalidates it. No-op on an invalid range. */
    void ReleaseAndFree(MaterialManager* materialManager, TriLightStore* triLightStore, Range& range);

    /** Repoints a filled slot at another material. Leaves the material refs to the caller, unlike FillEntry. */
    void SetMaterial(uint32_t slot, MaterialManager* materialManager, MaterialID material);

    void SetLightIndex(uint32_t slot, uint32_t lightIndex) { gpuInstances_[slot].lightIndex = lightIndex; }

    const InstanceSource* Get(Range range) const { return range.IsValid() ? &instances_[range.offset] : nullptr; }

    const InstanceSource& operator[](uint32_t i) const { return instances_[i]; }

    const InstanceSource* Data() const { return instances_.Data(); }

    const Instance& GetInstance(uint32_t i) const { return gpuInstances_[i]; }

    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] uint32_t GetWatermark() const { return ranges_.GetWatermark(); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    Core::VirtualArray<InstanceSource> instances_{};
    Core::VirtualArray<Instance> gpuInstances_{};
    Core::RangeAllocator ranges_{};
};
} // Engine

#endif //WILL_ENGINE_INSTANCE_STORE_H
