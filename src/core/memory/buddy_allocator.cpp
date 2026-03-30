//
// Created by William on 2026-03-30.
//

#include "buddy_allocator.h"

#include <cstring>

namespace Core
{
size_t BuddyAllocator::MetadataSize(size_t poolSize, size_t minBlockSize)
{
    const uint32_t minO = FloorOrder(minBlockSize);
    const uint32_t maxO = FloorOrder(poolSize);
    const uint32_t numO = maxO - minO + 1;
    const auto N = static_cast<uint32_t>(poolSize >> minO);
    return numO * sizeof(uint32_t) // freeHeads
           + N * sizeof(uint8_t) // blockState
           + N * sizeof(uint8_t); // allocOrder
}

void BuddyAllocator::Init(void* pool, size_t poolSizeIn, size_t minBlockSize, void* metadata)
{
    assert((poolSizeIn & (poolSizeIn - 1)) == 0 && "poolSize must be a power of 2");
    assert((minBlockSize & (minBlockSize - 1)) == 0 && "minBlockSize must be a power of 2");
    assert(minBlockSize >= sizeof(FreeNode) && "minBlockSize must be >= 8");

    base = static_cast<uint8_t*>(pool);
    poolSize = poolSizeIn;
    minOrder = FloorOrder(minBlockSize);
    maxOrder = FloorOrder(poolSizeIn);
    numOrders = maxOrder - minOrder + 1;

    const auto N = static_cast<uint32_t>(poolSizeIn >> minOrder);
    auto* meta = static_cast<uint8_t*>(metadata);

    freeHeads = reinterpret_cast<uint32_t*>(meta);
    meta += numOrders * sizeof(uint32_t);
    blockState = meta;
    meta += N;
    allocOrder = meta;

    for (uint32_t i = 0; i < numOrders; ++i) { freeHeads[i] = NONE; }
    memset(blockState, ALLOCATED, N);
    memset(allocOrder, 0, N);

    // Entire pool starts as one free block at maxOrder.
    blockState[0] = static_cast<uint8_t>(maxOrder);
    *Node(0) = {NONE, NONE};
    freeHeads[maxOrder - minOrder] = 0;
}

void* BuddyAllocator::Alloc(size_t size)
{
    if (size == 0) { return nullptr; }

    uint32_t order = CeilOrder(size);
    if (order < minOrder) { order = minOrder; }
    if (order > maxOrder) { return nullptr; }

    // Find the smallest order >= requested that has a free block.
    uint32_t found = order;
    while (found <= maxOrder && freeHeads[found - minOrder] == NONE) { ++found; }
    if (found > maxOrder) { return nullptr; }

    uint32_t idx = Pop(found);

    // Split the block down to the requested order, pushing the right half of each
    // split onto its order's free list.
    while (found > order) {
        --found;
        const uint32_t right = idx ^ (1u << (found - minOrder));
        blockState[right] = static_cast<uint8_t>(found);
        Push(found, right);
    }

    allocOrder[idx] = static_cast<uint8_t>(order);
    blockState[idx] = ALLOCATED;
    usedBytes += size_t(1) << order;
    return base + (static_cast<size_t>(idx) << minOrder);
}

void BuddyAllocator::Free(void* ptr)
{
    if (!ptr) { return; }

    auto idx = static_cast<uint32_t>((static_cast<uint8_t*>(ptr) - base) >> minOrder);
    uint32_t order = allocOrder[idx];
    usedBytes -= size_t(1) << order;

    // Coalesce upward: if the buddy is free at the same order, merge and repeat.
    while (order < maxOrder) {
        const uint32_t buddy = idx ^ (1u << (order - minOrder));
        if (blockState[buddy] != static_cast<uint8_t>(order)) { break; }
        Remove(order, buddy);
        blockState[buddy] = ALLOCATED;
        idx = (idx < buddy) ? idx : buddy; // merged block starts at the lower index
        ++order;
    }

    blockState[idx] = static_cast<uint8_t>(order);
    Push(order, idx);
}

// --- free list helpers ---

BuddyAllocator::FreeNode* BuddyAllocator::Node(uint32_t idx) const
{
    return reinterpret_cast<FreeNode*>(base + (static_cast<size_t>(idx) << minOrder));
}

void BuddyAllocator::Push(uint32_t order, uint32_t idx) const
{
    const uint32_t rel = order - minOrder;
    FreeNode* n = Node(idx);
    n->next = freeHeads[rel];
    n->prev = NONE;
    if (freeHeads[rel] != NONE) { Node(freeHeads[rel])->prev = idx; }
    freeHeads[rel] = idx;
}

uint32_t BuddyAllocator::Pop(uint32_t order) const
{
    const uint32_t rel = order - minOrder;
    const uint32_t idx = freeHeads[rel];
    assert(idx != NONE);
    freeHeads[rel] = Node(idx)->next;
    if (freeHeads[rel] != NONE) { Node(freeHeads[rel])->prev = NONE; }
    return idx;
}

void BuddyAllocator::Remove(uint32_t order, uint32_t idx) const
{
    const uint32_t rel = order - minOrder;
    const FreeNode n = *Node(idx); // copy before modifying neighbours
    if (n.prev == NONE) { freeHeads[rel] = n.next; }
    else { Node(n.prev)->next = n.next; }
    if (n.next != NONE) { Node(n.next)->prev = n.prev; }
}
} // Core
