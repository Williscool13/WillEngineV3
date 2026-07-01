//
// Created by William on 2026-07-01.
//

#ifndef WILL_ENGINE_RANGE_ALLOCATOR_H
#define WILL_ENGINE_RANGE_ALLOCATOR_H

#include <cstdint>

#include "core/memory/tlsf_allocator.h"
#include "core/containers/vector.h"

namespace Core
{
/**
 * Out-of-band suballocator over slot space [0, capacity). Hands out stable contiguous Ranges and coalesces on Free. Not thread-safe.
 */
class RangeAllocator
{
public:
    struct Range
    {
        uint32_t offset{0};
        uint32_t count{0};

        [[nodiscard]] bool IsValid() const { return count != 0; }
    };

    void Init(uint32_t capacity, TlsfAllocator* alloc, AllocTag tag = AllocTag::Unknown);

    void Reset();

    Range Allocate(uint32_t count);

    void Free(Range range);

    struct Stats
    {
        uint32_t capacity;
        uint32_t used;
        uint32_t largestFreeRun;
        uint32_t freeSpanCount;
    };

    [[nodiscard]] Stats GetStats() const;

    [[nodiscard]] uint32_t GetCapacity() const { return capacity_; }
    [[nodiscard]] uint32_t GetUsed() const { return used_; }
    [[nodiscard]] bool IsInitialized() const { return capacity_ != 0; }

private:
    Vector<Range> freeSpans_{};
    uint32_t capacity_{0};
    uint32_t used_{0};
};
} // Core

#endif //WILL_ENGINE_RANGE_ALLOCATOR_H
