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
 * The group slot IS the GPU EmissiveGroup index. Two budgets, both enforced here: MAX_EMISSIVE_GROUPS group slots (one GPU work item each) and the shared triangle capacity.
 * The GPU builds into the frame's LightData slot, so dirty bits carry one set per host slot like the CPU-written stores; a released reservation is written dead into every slot before reuse. Not thread-safe.
 */
class TriLightStore
{
public:
    using Range = Core::RangeAllocator::Range;

    static constexpr uint32_t INVALID_GROUP = ~0u;
    static constexpr uint32_t REUSE_DELAY_FRAMES = 4;

    struct Reservation
    {
        uint32_t instanceSlot{~0u};
        Range range{};
        bool bLive{false};
    };

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag = Core::AllocTag::Render);

    /** @returns the group slot, or INVALID_GROUP when either budget is exhausted. */
    uint32_t Reserve(uint32_t instanceSlot, uint32_t triangleCount, const char* ownerName);

    void Release(uint32_t groupSlot);

    void MarkDirty(uint32_t groupSlot) { dirty_.Mark(groupSlot); }

    void MarkAllDirty() { dirty_.MarkRange(0, GetGroupWatermark()); }

    /** Switching on re-marks everything; the slots hold stale data from before. */
    void SetEnabled(bool bEnabled);

    void Tick(uint64_t frame);

    [[nodiscard]] const Reservation& Get(uint32_t groupSlot) const { return reservations_[groupSlot]; }

    /** Group slots dirty for this host slot; emit(groupSlot). */
    template<typename Fn>
    void DrainDirty(uint32_t setIndex, Fn&& emit)
    {
        dirty_.Drain(setIndex, GetGroupWatermark(), [&](uint32_t offset, uint32_t count) {
            for (uint32_t i = 0; i < count; ++i) { emit(offset + i); }
        });
    }

    [[nodiscard]] uint32_t GetReservationCount() const { return reservationCount_; }
    [[nodiscard]] uint32_t GetGroupWatermark() const { return groupSlots_.GetWatermark(); }
    [[nodiscard]] uint32_t GetWatermark() const { return ranges_.GetWatermark(); }
    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] uint32_t GetPendingFreeCount() const { return static_cast<uint32_t>(pendingFrees_.Size()); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    struct PendingFree
    {
        uint32_t groupSlot{INVALID_GROUP};
        uint64_t frame{0};
    };

    Core::RangeAllocator ranges_{};
    Core::RangeAllocator groupSlots_{};
    Core::HeapArray<Reservation> reservations_{};
    Core::DirtyBits dirty_{};
    Core::Vector<PendingFree> pendingFrees_{};
    uint32_t reservationCount_{0};
    uint64_t frame_{0};
    bool bEnabled_{true};
    bool bWarnedFull_{false};
    bool bWarnedCapReached_{false};
};
} // Engine

#endif //WILL_ENGINE_TRI_LIGHT_STORE_H
