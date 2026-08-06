//
// Created by William on 2026-03-30.
//

#ifndef WILL_ENGINE_BUDDY_ALLOCATOR_H
#define WILL_ENGINE_BUDDY_ALLOCATOR_H

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "core/containers/inline_string.h"

namespace Core
{
// Returns the smallest k such that (1 << k) >= v.
inline uint32_t CeilOrder(size_t v)
{
    uint32_t k = 0;
    while ((static_cast<size_t>(1) << k) < v) { ++k; }
    return k;
}

// Returns the largest k such that (1 << k) <= v.
inline uint32_t FloorOrder(size_t v)
{
    uint32_t k = 0;
    while ((static_cast<size_t>(1) << (k + 1)) <= v) { ++k; }
    return k;
}

/**
 * Power-of-2 buddy allocator. Suballocates from an externally-provided pool.
 * Supports O(log N) alloc and free with full coalescing.
 *
 * Constraints:
 *   - pool_size and min_block_size must be powers of 2
 *   - min_block_size must be >= 8 bytes (free blocks store linked-list links in their memory)
 *   - Allocations are naturally aligned to their size
 *
 * Usage:
 *   size_t meta = BuddyAllocator::MetadataSize(pool_size, min_block_size);
 *   void* meta_buf = ...; // separate allocation for bookkeeping
 *   BuddyAllocator buddy;
 *   buddy.Init(pool_ptr, pool_size, min_block_size, meta_buf);
 *   void* p = buddy.Alloc(n);
 *   buddy.Free(p);
 */
class BuddyAllocator
{
public:
    static constexpr uint32_t NONE      = UINT32_MAX;
    static constexpr uint8_t  ALLOCATED = 0xFF;

    /**
     * Returns how many bytes the metadata buffer must be for the given pool parameters.
     */
    static size_t MetadataSize(size_t poolSize, size_t minBlockSize);

    void  Init(void* pool, size_t poolSize, size_t minBlockSize, void* metadata, const char* name = "");
    void* Alloc(size_t size);
    void  Free(void* ptr);

    struct Stats
    {
        size_t totalBytes;
        size_t usedBytes;
        size_t freeBytes;
    };

    [[nodiscard]] Stats  GetStats()    const { return {poolSize, usedBytes, poolSize - usedBytes}; }
    [[nodiscard]] size_t GetPoolSize() const { return poolSize; }
    [[nodiscard]] size_t GetMinBlock() const { return size_t(1) << minOrder; }
    [[nodiscard]] const char* GetName() const { return name_.buf; }

private:
    // Stored at the start of each free block — overlaps with user memory (safe: not in use).
    struct FreeNode
    {
        uint32_t next;
        uint32_t prev;
    };

    uint8_t*  base{};
    size_t    poolSize{};
    InlineString<32> name_{};
    size_t    usedBytes{};
    uint32_t  minOrder{};    // log2(min_block_size)
    uint32_t  maxOrder{};    // log2(pool_size)
    uint32_t  numOrders{};   // max_order - min_order + 1

    // Metadata arrays (laid out inside the metadata buffer passed to Init)
    uint32_t* freeHeads{};   // [numOrders]  head min-block index per order, or NONE
    uint8_t*  blockState{};  // [N]  order if block is free, ALLOCATED if not
    uint8_t*  allocOrder{};  // [N]  order at time of allocation (survives until Free)

    FreeNode* Node(uint32_t idx) const;

    void     Push(uint32_t order, uint32_t idx) const;
    uint32_t Pop(uint32_t order) const;
    void     Remove(uint32_t order, uint32_t idx) const;
};
} // Core

#endif //WILL_ENGINE_BUDDY_ALLOCATOR_H
