//
// Created by William on 2026-01-27.
//

#ifndef WILL_ENGINE_LOCK_FREE_HANDLE_ALLOCATOR_H
#define WILL_ENGINE_LOCK_FREE_HANDLE_ALLOCATOR_H

#include <atomic>
#include <cstdint>
#include <array>

#include "handle.h"

namespace Core
{
template<typename T, size_t MaxSize>
class LockFreeHandleAllocator
{
    struct Node
    {
        std::atomic<uint32_t> generation;
        std::atomic<uint32_t> nextFree;
    };

    std::array<Node, MaxSize> nodes;
    std::atomic<uint32_t> freeListHead{0};
    std::atomic<uint32_t> count{0};

    static constexpr uint32_t INVALID = ~0u;

public:
    LockFreeHandleAllocator()
    {
        for (uint32_t i = 0; i < MaxSize; ++i) {
            nodes[i].generation.store(1, std::memory_order_relaxed);
            nodes[i].nextFree.store(i + 1, std::memory_order_relaxed);
        }
        nodes[MaxSize - 1].nextFree.store(INVALID, std::memory_order_relaxed);
    }

    Handle<T> Add()
    {
        uint32_t index = freeListHead.load(std::memory_order_acquire);

        while (index != INVALID) {
            uint32_t next = nodes[index].nextFree.load(std::memory_order_acquire);

            if (freeListHead.compare_exchange_weak(index, next, std::memory_order_release, std::memory_order_acquire)) {
                uint32_t gen = nodes[index].generation.load(std::memory_order_acquire);
                count.fetch_add(1, std::memory_order_relaxed);
                return {index, gen};
            }
        }

        return {INVALID, INVALID};
    }

    bool Remove(Handle<T> handle)
    {
        if (handle.index >= MaxSize) return false;

        uint32_t expected = handle.generation;
        if (!nodes[handle.index].generation.compare_exchange_strong(expected, expected + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
            // Already released (issue).
            return false;
        }

        uint32_t oldHead = freeListHead.load(std::memory_order_acquire);
        do {
            nodes[handle.index].nextFree.store(oldHead, std::memory_order_release);
        } while (!freeListHead.compare_exchange_weak(oldHead, handle.index, std::memory_order_release, std::memory_order_acquire));

        count.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    bool IsValid(Handle<T> handle) const
    {
        if (handle.index >= MaxSize) return false;
        return nodes[handle.index].generation.load(std::memory_order_acquire) == handle.generation;
    }

    uint32_t GetCount() const { return count.load(std::memory_order_relaxed); }
    constexpr uint32_t GetCapacity() const { return MaxSize; }
};
} // Core

#endif //WILL_ENGINE_LOCK_FREE_HANDLE_ALLOCATOR_H
