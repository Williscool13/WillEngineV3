//
// Created by William on 2026-08-08.
//

#ifndef WILL_ENGINE_TRI_LIGHT_STORE_H
#define WILL_ENGINE_TRI_LIGHT_STORE_H

#include <cstdint>

#include "core/containers/vector.h"
#include "core/memory/range_allocator.h"

namespace Engine
{
struct StaticModel;

/**
 * Stable range space for emissive-triangle lights. A range covers one emissive primitive's full triangle set, so base + PrimitiveIndex() resolves a ray hit to its own light, and the GPU light index is MAX_ANALYTIC_LIGHTS + range offset.
 */
class TriLightStore
{
public:
    using Range = Core::RangeAllocator::Range;

    static constexpr uint32_t REUSE_DELAY_FRAMES = 4;

    void Init(uint32_t capacity, Core::TlsfAllocator* alloc, Core::AllocTag tag = Core::AllocTag::Render);

    Range Allocate(uint32_t triangleCount);

    void Free(Range& range);

    void Tick(uint64_t frame);

    [[nodiscard]] uint32_t GetWatermark() const { return ranges_.GetWatermark(); }
    [[nodiscard]] Core::RangeAllocator::Stats GetStats() const { return ranges_.GetStats(); }
    [[nodiscard]] uint32_t GetPendingFreeCount() const { return static_cast<uint32_t>(pendingFrees_.Size()); }
    [[nodiscard]] bool IsInitialized() const { return ranges_.IsInitialized(); }

private:
    struct PendingFree
    {
        Range range{};
        uint64_t frame{0};
    };

    Core::RangeAllocator ranges_{};
    Core::Vector<PendingFree> pendingFrees_{};
    uint64_t frame_{0};
    bool bWarnedFull_{false};
};
} // Engine

#endif //WILL_ENGINE_TRI_LIGHT_STORE_H
