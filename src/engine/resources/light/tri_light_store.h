//
// Created by William on 2026-08-08.
//

#ifndef WILL_ENGINE_TRI_LIGHT_STORE_H
#define WILL_ENGINE_TRI_LIGHT_STORE_H

#include <cstdint>

#include "core/containers/heap_array.h"
#include "core/containers/vector.h"
#include "core/memory/dirty_bits.h"
#include "core/memory/range_allocator.h"
#include "render/shaders/lights_interop.h"

namespace Engine
{
struct StaticModel;

/**
 * Stable range space for emissive-triangle lights, and the registry of who holds one.
 * A reservation covers one emissive primitive's full triangle set, so base + PrimitiveIndex() resolves a ray hit to its own light, and the GPU light index is MAX_ANALYTIC_LIGHTS + range offset.
 * The reservation slot is the GPU EmissiveMesh index; its meshlets are a contiguous run of EmissiveMeshlet slots, one per LOD0 meshlet.
 * Budgets: MAX_EMISSIVE_MESHES reservations, MAX_EMISSIVE_MESHLETS meshlet slots, and the triangle capacity.
 * The GPU builds into the frame's LightData slot, so dirty bits carry one set per host slot like the CPU-written stores; a released reservation is written dead into every slot before reuse. Not thread-safe.
 */
class TriLightStore
{
public:
    using Range = Core::RangeAllocator::Range;

    static constexpr uint32_t INVALID_MESH_SLOT = ~0u;
    static constexpr uint32_t REUSE_DELAY_FRAMES = 4;

    struct Reservation
    {
        uint32_t instanceSlot{~0u};
        Range range{};
        Range meshlets{};
        bool bLive{false};
    };

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag = Core::AllocTag::Render);

    /** @returns the mesh slot, or INVALID_MESH_SLOT when the mesh or triangle budget is exhausted. A refused meshlet run falls back to one meshlet. */
    uint32_t Reserve(uint32_t instanceSlot, uint32_t triangleCount, uint32_t meshletCount, const char* ownerName);

    void Release(uint32_t meshSlot);

    void MarkDirty(uint32_t meshSlot) { dirty_.Mark(meshSlot); }

    void MarkAllDirty() { dirty_.MarkRange(0, GetMeshWatermark()); }

    /** Switching on re-marks everything; the slots hold stale data from before. */
    void SetEnabled(bool bEnabled);

    void Tick(uint64_t frame);

    [[nodiscard]] const Reservation& Get(uint32_t meshSlot) const { return reservations_[meshSlot]; }

    /** Mesh slots dirty for this host slot; emit(meshSlot). */
    template<typename Fn>
    void DrainDirty(uint32_t setIndex, Fn&& emit)
    {
        dirty_.Drain(setIndex, GetMeshWatermark(), [&](uint32_t offset, uint32_t count) {
            for (uint32_t i = 0; i < count; ++i) { emit(offset + i); }
        });
    }

    [[nodiscard]] uint32_t GetReservationCount() const { return reservationCount_; }
    [[nodiscard]] uint32_t GetMeshWatermark() const { return meshes_.GetWatermark(); }
    [[nodiscard]] uint32_t GetMeshletWatermark() const { return meshlets_.GetWatermark(); }
    [[nodiscard]] uint32_t GetWatermark() const { return ranges_.GetWatermark(); }
    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] uint32_t GetPendingFreeCount() const { return static_cast<uint32_t>(pendingFrees_.Size()); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    struct PendingFree
    {
        uint32_t meshSlot{INVALID_MESH_SLOT};
        uint64_t frame{0};
    };

    Core::RangeAllocator ranges_{};
    Core::RangeAllocator meshes_{};
    Core::RangeAllocator meshlets_{};
    Core::HeapArray<Reservation> reservations_{};
    Core::DirtyBits dirty_{};
    Core::Vector<PendingFree> pendingFrees_{};
    uint32_t reservationCount_{0};
    uint64_t frame_{0};
    bool bEnabled_{true};
    bool bWarnedFull_{false};
    bool bWarnedCapReached_{false};
    bool bWarnedMeshletsFull_{false};
};
} // Engine

#endif //WILL_ENGINE_TRI_LIGHT_STORE_H
