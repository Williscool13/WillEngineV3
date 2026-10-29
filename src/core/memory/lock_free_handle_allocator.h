//
// Created by William on 2026-01-27.
//

#ifndef WILL_ENGINE_LOCK_FREE_HANDLE_ALLOCATOR_H
#define WILL_ENGINE_LOCK_FREE_HANDLE_ALLOCATOR_H

#include <atomic>
#include <cstdint>

#include "handle.h"
#include "core/containers/array.h"

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

    static_assert(std::atomic<uint64_t>::is_always_lock_free);

    Array<Node, MaxSize> nodes;
    std::atomic<uint64_t> freeListHead{0};
    std::atomic<uint32_t> count{0};

    static constexpr uint32_t INVALID = ~0u;

    static uint64_t PackHead(uint32_t index, uint32_t generation) { return static_cast<uint64_t>(generation) << 32 | index; }
    static uint32_t HeadIndex(uint64_t head) { return static_cast<uint32_t>(head); }

public:
    LockFreeHandleAllocator() : LockFreeHandleAllocator(MaxSize) {}

    /** Only [0, initialCount) start free; Grow hands out the rest. */
    explicit LockFreeHandleAllocator(uint32_t initialCount)
    {
        for (uint32_t i = 0; i < MaxSize; ++i) {
            nodes[i].generation.store(1, std::memory_order_relaxed);
            nodes[i].nextFree.store(i + 1 < initialCount ? i + 1 : INVALID, std::memory_order_relaxed);
        }
        freeListHead.store(initialCount > 0 ? PackHead(0, 1) : PackHead(INVALID, 0), std::memory_order_relaxed);
    }

    /** Pushes never-used indices [begin, end) onto the free list, begin on top. Each index must be pushed once. */
    void Grow(uint32_t begin, uint32_t end)
    {
        if (begin >= end) { return; }
        for (uint32_t i = begin; i + 1 < end; ++i) {
            nodes[i].nextFree.store(i + 1, std::memory_order_relaxed);
        }
        const uint64_t newHead = PackHead(begin, nodes[begin].generation.load(std::memory_order_relaxed));
        uint64_t oldHead = freeListHead.load(std::memory_order_acquire);
        do {
            nodes[end - 1].nextFree.store(HeadIndex(oldHead), std::memory_order_release);
        } while (!freeListHead.compare_exchange_weak(oldHead, newHead, std::memory_order_release, std::memory_order_acquire));
    }

    Handle<T> Add()
    {
        uint64_t head = freeListHead.load(std::memory_order_acquire);

        while (HeadIndex(head) != INVALID) {
            const uint32_t index = HeadIndex(head);
            const uint32_t next = nodes[index].nextFree.load(std::memory_order_acquire);
            const uint64_t nextHead = next == INVALID ? PackHead(INVALID, 0) : PackHead(next, nodes[next].generation.load(std::memory_order_acquire));

            if (freeListHead.compare_exchange_weak(head, nextHead, std::memory_order_acq_rel, std::memory_order_acquire)) {
                const uint32_t gen = nodes[index].generation.load(std::memory_order_acquire);
                count.fetch_add(1, std::memory_order_relaxed);
                return {index, gen};
            }
        }

        return Handle<T>::INVALID;
    }

    bool Remove(Handle<T> handle)
    {
        if (handle.index >= MaxSize) return false;

        uint32_t expected = handle.generation;
        uint32_t nextGeneration = expected + 1;
        if (nextGeneration == INVALID_HANDLE_GENERATION) { nextGeneration = 1; }
        if (!nodes[handle.index].generation.compare_exchange_strong(expected, nextGeneration, std::memory_order_acq_rel, std::memory_order_acquire)) {
            // Already released (issue).
            return false;
        }

        const uint64_t newHead = PackHead(handle.index, nextGeneration);
        uint64_t oldHead = freeListHead.load(std::memory_order_acquire);
        do {
            nodes[handle.index].nextFree.store(HeadIndex(oldHead), std::memory_order_release);
        } while (!freeListHead.compare_exchange_weak(oldHead, newHead, std::memory_order_release, std::memory_order_acquire));

        count.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    bool IsValid(Handle<T> handle) const
    {
        if (handle.index >= MaxSize) return false;
        return nodes[handle.index].generation.load(std::memory_order_acquire) == handle.generation;
    }

    /**
     * Thread safe (atomic)
     * @return
     */
    uint32_t GetCount() const { return count.load(std::memory_order_relaxed); }
    constexpr uint32_t GetCapacity() const { return MaxSize; }
};
} // Core

#endif //WILL_ENGINE_LOCK_FREE_HANDLE_ALLOCATOR_H
