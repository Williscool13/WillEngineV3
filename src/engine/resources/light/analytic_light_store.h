//
// Created by William on 2026-08-08.
//

#ifndef WILL_ENGINE_ANALYTIC_LIGHT_STORE_H
#define WILL_ENGINE_ANALYTIC_LIGHT_STORE_H

#include <cstdint>

#include "core/containers/vector.h"
#include "core/memory/range_allocator.h"

namespace Engine
{
/**
 * Stable slot space for analytic (area/sphere) lights over [0, MAX_ANALYTIC_LIGHTS). The slot is the GPU light index and dead slots upload as zeroed LightInfo (intensity 0).
 * Owners hold the slot on their light component. Freed slots are quarantined for REUSE_DELAY_FRAMES ticks so carried ReSTIR/ReGIR reservoirs (history depth <= 2 with checkerboarding) re-evaluate them as dead instead of aliasing a newly spawned light. Not thread-safe.
 */
class AnalyticLightStore
{
public:
    static constexpr uint32_t INVALID_SLOT = ~0u;
    static constexpr uint32_t REUSE_DELAY_FRAMES = 4;

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag = Core::AllocTag::Render);

    uint32_t Allocate();

    void Free(uint32_t& slot);

    void Tick(uint64_t frame);

    [[nodiscard]] uint32_t GetWatermark() const { return ranges_.GetWatermark(); }
    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] uint32_t GetPendingFreeCount() const { return static_cast<uint32_t>(pendingFrees_.Size()); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    struct PendingFree
    {
        Core::RangeAllocator::Range range{};
        uint64_t frame{0};
    };

    Core::RangeAllocator ranges_{};
    Core::Vector<PendingFree> pendingFrees_{};
    uint64_t frame_{0};
};
} // Engine

#endif //WILL_ENGINE_ANALYTIC_LIGHT_STORE_H
