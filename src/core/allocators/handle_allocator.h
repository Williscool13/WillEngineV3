//
// Created by William on 2025-10-20.
//

#ifndef WILLENGINETESTBED_HANDLE_ALLOCATOR_H
#define WILLENGINETESTBED_HANDLE_ALLOCATOR_H

#include <array>
#include <vector>

#include "handle.h"
#include "ring_buffer.h"

namespace Core
{
template<typename T, size_t MaxSize>
class HandleAllocator
{
    std::vector<uint32_t> generations;
    RingBuffer<uint32_t, MaxSize> freeIndices;
    uint32_t count = 0;

public:
    HandleAllocator()
    {
        generations.resize(MaxSize, 1);

        // Push indices 0 to MaxSize-1 in order
        for (uint32_t i = 0; i < MaxSize; ++i) {
            freeIndices.Push(i);
        }
    }

    Handle<T> Add()
    {
        uint32_t index;
        if (!freeIndices.Pop(index)) {
            return Handle<T>(INVALID_HANDLE_INDEX, INVALID_HANDLE_GENERATION);
        }

        ++count;

        return {index, generations[index]};
    }

    bool Remove(Handle<T> handle)
    {
        if (!IsValid(handle)) {
            return false;
        }

        ++generations[handle.index];
        freeIndices.Push(handle.index);
        --count;
        return true;
    }

    void Clear()
    {
        freeIndices.Clear();
        for (uint32_t i = 0; i < MaxSize; ++i) {
            freeIndices.Push(i);
            ++generations[i]; // invalidate all existing handles
        }
        count = 0;
    }

    bool IsValid(Handle<T> handle) const
    {
        if (handle.index >= MaxSize) { return false; }
        if (generations[handle.index] != handle.generation) { return false; }
        return true;
    }

    [[nodiscard]] bool IsAnyFree() const
    {
        return !freeIndices.IsEmpty();
    }

    [[nodiscard]] uint32_t GetCount() const { return count; }
    [[nodiscard]] uint32_t GetCapacity() const { return MaxSize; }
};
}

#endif //WILLENGINETESTBED_HANDLE_ALLOCATOR_H