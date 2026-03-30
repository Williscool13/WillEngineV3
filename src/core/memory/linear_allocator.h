//
// Created by William on 2026-01-04.
//

#ifndef WILL_ENGINE_LINEAR_ALLOCATOR_H
#define WILL_ENGINE_LINEAR_ALLOCATOR_H

#include <cassert>
#include <cstddef>
#include <optional>

namespace Core
{
class LinearAllocator
{
    size_t head = 0;
    size_t capacity;

public:
    explicit LinearAllocator(size_t size) : capacity(size)
    {
        assert(size > 0 && size < SIZE_MAX);
    }

    [[nodiscard]] static LinearAllocator CreateExpanded(const LinearAllocator& old, size_t newCapacity)
    {
        assert(newCapacity >= old.capacity && "New capacity must be >= old capacity");
        assert(newCapacity > 0 && newCapacity < SIZE_MAX);

        LinearAllocator expanded(newCapacity);
        expanded.head = old.head;
        return expanded;
    }

    size_t Allocate(size_t size, size_t alignment = 1)
    {
        size_t alignedHead = (head + alignment - 1) & ~(alignment - 1);

        if (alignedHead + size > capacity) {
            return SIZE_MAX;
        }

        head = alignedHead + size;
        return alignedHead;
    }

    void Reset() { head = 0; }

    [[nodiscard]] size_t GetUsed() const { return head; }
    [[nodiscard]] size_t GetCapacity() const { return capacity; }
    [[nodiscard]] size_t GetRemaining() const { return capacity - head; }
};
} // Core

#endif //WILL_ENGINE_LINEAR_ALLOCATOR_H
