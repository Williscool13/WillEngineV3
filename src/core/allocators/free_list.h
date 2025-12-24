//
// Created by William on 2025-10-19.
//

#ifndef WILLENGINETESTBED_FREE_LIST_H
#define WILLENGINETESTBED_FREE_LIST_H
#include <array>
#include <vector>

#include "handle.h"
#include "ring_buffer.h"

namespace Core
{
/**
 * Free list data structure that owns the array of T
 * @tparam T
 * @tparam MaxSize maximum allocations of T
 */
template<typename T, size_t MaxSize>
class FreeList
{
    std::vector<T> slots;
    std::vector<uint32_t> generations;

    RingBuffer<uint32_t, MaxSize> freeIndices;
    uint32_t count = 0;

public:
    FreeList()
    {
        slots.resize(MaxSize);
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

    Handle<T> Add(T data)
    {
        uint32_t index;
        if (!freeIndices.Pop(index)) {
            return Handle<T>(INVALID_HANDLE_INDEX, INVALID_HANDLE_GENERATION);
        }
        ++count;
        slots[index] = std::move(data);
        return {index, generations[index]};
    }

    T* Get(Handle<T> handle)
    {
        if (handle.index >= MaxSize) { return nullptr; }
        if (generations[handle.index] != handle.generation) { return nullptr; }
        return &slots[handle.index];
    }

    bool Remove(Handle<T> handle)
    {
        if (auto* item = Get(handle)) {
            ++generations[handle.index];
            freeIndices.Push(handle.index); // Changed from push_back
            slots[handle.index].~T();
            --count;
            return true;
        }
        return false;
    }

    void Clear()
    {
        freeIndices.Clear();
        for (uint32_t i = 0; i < MaxSize; ++i) {
            freeIndices.Push(i);
            ++generations[i];
        }
        count = 0;
    }

    [[nodiscard]] bool IsAnyFree() const { return !freeIndices.IsEmpty(); }

    /**
     * Use sparingly, mostly for initialization/deinitialization and debugging
     * @return
     */
    std::vector<T>& GetAllSlots() { return slots; }
};
} // Core


#endif //WILLENGINETESTBED_FREE_LIST_H
