//
// Created by William on 2025-10-19.
//

#ifndef WILLENGINETESTBED_FREE_LIST_H
#define WILLENGINETESTBED_FREE_LIST_H
#include <array>
#include <vector>

#include "handle.h"

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

    std::vector<uint32_t> freeIndices;
    uint32_t count = 0;

public:
    FreeList()
    {
        slots.resize(MaxSize);
        generations.resize(MaxSize);
        freeIndices.reserve(MaxSize);
        for (uint32_t i = 0; i < MaxSize; ++i) {
            freeIndices.push_back(MaxSize - 1 - i);
        }
    }

    Handle<T> Add()
    {
        if (freeIndices.empty()) {
            return Handle<T>(INVALID_HANDLE_INDEX, INVALID_HANDLE_GENERATION);
        }
        uint32_t index = freeIndices.back();
        freeIndices.pop_back();
        ++count;

        return {index, generations[index]};
    }

    Handle<T> Add(T data)
    {
        if (freeIndices.empty()) {
            return Handle<T>(INVALID_HANDLE_INDEX, INVALID_HANDLE_GENERATION);
        }
        uint32_t index = freeIndices.back();
        freeIndices.pop_back();
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
            freeIndices.push_back(handle.index);
            slots[handle.index].~T();
            --count;
            return true;
        }

        return false;
    }

    void Clear()
    {
        freeIndices.clear();
        for (uint32_t i = 0; i < MaxSize; ++i) {
            freeIndices.push_back(MaxSize - 1 - i);
            ++generations[i]; // invalidate all existing handles
        }
        count = 0;
    }

    [[nodiscard]] bool IsAnyFree() const { return count < MaxSize; }

    /**
     * Use sparingly, mostly for initialization/deinitialization and debugging
     * @return
     */
    std::vector<T>& GetAllSlots() { return slots; }
};
} // Core


#endif //WILLENGINETESTBED_FREE_LIST_H
