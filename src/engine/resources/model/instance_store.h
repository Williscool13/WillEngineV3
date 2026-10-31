//
// Created by William on 2026-07-01.
//

#ifndef WILL_ENGINE_INSTANCE_STORE_H
#define WILL_ENGINE_INSTANCE_STORE_H

#include <cstdint>

#include "core/containers/span.h"
#include "core/containers/vector.h"
#include "core/containers/virtual_array.h"
#include "core/memory/dirty_bits.h"
#include "core/memory/range_allocator.h"
#include "core/types/math.h"
#include "engine/core/access_set.h"
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
    // Primitive bounding sphere in its node's space
    Vec4 localBounds{0.0f};
    uint64_t stableId{0};
    uint32_t lightIndex{~0u};
    uint32_t flags{INSTANCE_FLAG_MOTION_BLUR | INSTANCE_FLAG_ALPHA_CUTOUT | INSTANCE_FLAG_DDGI_VISIBLE | INSTANCE_FLAG_CAMERA_MOTION_BLUR};
    bool bVisible{true};

    /** TriLightStore mesh slot covering this primitive's full emissive triangle set */
    uint32_t emissiveMeshSlot{~0u};
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

/** World spheres of everything added, moved, hidden, shown, re-materialed or removed since the last clear. */
class InstanceBoundsChanges
{
public:
    static constexpr const char* COMPONENT_NAME = "InstanceBoundsChanges";

    void Push(const Vec4& sphere) { changes.PushBack(sphere); }

    [[nodiscard]] Core::Span<const Vec4> Get() const { return {changes.Data(), changes.Size()}; }

    void Clear() { changes.Clear(); }

private:
    Core::Vector<Vec4> changes{};

    friend class InstanceStore;
};

/** Last world sphere per slot. */
class InstanceWorldBounds
{
public:
    static constexpr const char* COMPONENT_NAME = "InstanceWorldBounds";

    [[nodiscard]] const Vec4& Get(uint32_t slot) const { return bounds[slot]; }

    /** The previous sphere and the new one both join the bounds changes. */
    void Set(uint32_t slot, const Vec4& sphere, InstanceBoundsChanges& changes)
    {
        PushChange(slot, changes);
        bounds[slot] = sphere;
        PushChange(slot, changes);
    }

private:
    void PushChange(uint32_t slot, InstanceBoundsChanges& changes) const
    {
        if (bounds[slot].w > 0.0f) { changes.Push(bounds[slot]); }
    }

    Core::VirtualArray<Vec4> bounds{};

    friend class InstanceStore;
};

/** Slots whose GPU Instance changed, one set per host buffer slot. */
class InstanceDirty
{
public:
    static constexpr const char* COMPONENT_NAME = "InstanceDirty";

    /** Fn: emit(offset, count) */
    template<typename Fn>
    void Drain(uint32_t setIndex, uint32_t limit, Fn&& emit) { bits.Drain(setIndex, limit, std::forward<Fn>(emit)); }

private:
    Core::DirtyBits bits{};

    friend class InstanceStore;
};

/**
 * InstanceSource is the authority; the GPU Instance array is a projection of it rewritten on every mutation, dead while the slot is not visible. Nothing hands out a mutable InstanceSource.
 */
class InstanceRecords
{
public:
    static constexpr const char* COMPONENT_NAME = "InstanceRecords";

    const InstanceSource& operator[](uint32_t i) const { return sources[i]; }

    [[nodiscard]] const Instance* Instances() const { return gpuInstances.Data(); }

    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges.GetStats(); }
    [[nodiscard]] uint32_t GetWatermark() const { return ranges.GetWatermark(); }
    [[nodiscard]] bool IsInitialized() const { return ranges.IsInitialized(); }

private:
    Core::VirtualArray<InstanceSource> sources{};
    Core::VirtualArray<Instance> gpuInstances{};
    Core::RangeAllocator ranges{};

    friend class InstanceStore;
};

/**
 * The stable instance slot space: one slot per flattened mesh primitive (static, static-primitive, procedural, spline, text3d, light surfaces). A RangeAllocator hands out one contiguous run per entity; the slot index IS the GPU instance index. Raw Allocate/Free leave material lifetimes to callers; AllocateSingleMeshRange/ReleaseAndFree manage the per-entry material refs and tri-light reservations. Callers own GPU uploads. Not thread-safe.
 * Every record write also dirties the slot's tri-light mesh, which is why those methods take the TriLightStore.
 */
class InstanceStore
{
public:
    static constexpr const char* COMPONENT_NAME = "InstanceStore";
    using ACCESS_PARTS = AccessParts<InstanceRecords, InstanceWorldBounds, InstanceBoundsChanges, InstanceDirty>;

    using Range = Core::RangeAllocator::Range;
    static constexpr Instance DEAD_INSTANCE{.primitiveIndex = DEAD_SLOT_PRIMITIVE_INDEX};

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::VirtualMemoryManager* vm, Core::AllocTag tag = Core::AllocTag::RenderMesh);

    Range Allocate(uint32_t count);

    Range AllocateSingleMeshRange(MaterialManager* materialManager, TriLightStore* triLightStore, StaticModel* model, MaterialID material, uint32_t modelSlot, bool bEmissiveLight);

    void ReleaseAndFree(MaterialManager* materialManager, TriLightStore* triLightStore, Range& range);

    // Writes
    void FillEntry(uint32_t slot, MaterialManager* materialManager, TriLightStore* triLightStore, StaticModel* model, const PrimitiveProperty& primitive, const InstanceFill& fill);

    void SetMaterial(uint32_t slot, MaterialManager* materialManager, TriLightStore* triLightStore, MaterialID material);

    void SetLightIndex(uint32_t slot, TriLightStore* triLightStore, uint32_t lightIndex);

    void SetRenderState(Range range, TriLightStore* triLightStore, bool bVisible, uint32_t flags, uint64_t stableId);

    const InstanceSource& operator[](uint32_t i) const { return records[i]; }

    void MarkAllDirty() { dirty.bits.MarkRange(0, GetWatermark()); }

    [[nodiscard]] uint32_t VerifyRecords(const TriLightStore* triLightStore) const;

    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return records.GetStats(); }
    [[nodiscard]] uint32_t GetWatermark() const { return records.GetWatermark(); }
    [[nodiscard]] bool IsInitialized() const { return records.IsInitialized(); }

    [[nodiscard]] InstanceRecords* GetRecords() { return &records; }
    [[nodiscard]] InstanceWorldBounds* GetWorldBounds() { return &worldBounds; }
    [[nodiscard]] InstanceBoundsChanges* GetBoundsChanges() { return &boundsChanges; }
    [[nodiscard]] InstanceDirty* GetDirty() { return &dirty; }

private:
    /** Private because it returns the slots without releasing their materials or tri-light runs. */
    void Free(Range range);

    [[nodiscard]] Instance MakeRecord(uint32_t slot, const TriLightStore* triLightStore) const;

    void WriteRecord(uint32_t slot, TriLightStore* triLightStore);

    InstanceRecords records{};
    InstanceWorldBounds worldBounds{};
    InstanceBoundsChanges boundsChanges{};
    InstanceDirty dirty{};
};
} // Engine

#endif //WILL_ENGINE_INSTANCE_STORE_H
