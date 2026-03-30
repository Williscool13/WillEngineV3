//
// Created by William on 2026-03-29.
//

#ifndef WILL_ENGINE_ARENA_VECTOR_H
#define WILL_ENGINE_ARENA_VECTOR_H

#include <cassert>
#include <cstddef>

namespace Core
{
/**
 * Non-resizing vector with runtime capacity. Data is stored in an external buffer provided
 * at construction — typically an arena allocation. The ArenaVector does not own the backing
 * memory and will not free it.
 *
 * Use ArenaVector when the maximum count is only known at runtime.
 * Use InlineVector when the maximum count is a known compile-time constant.
 *
 * Typical usage:
 *   ArenaVector<Foo> v{arena.AllocArray<Foo>(maxCount), maxCount};
 */
template<typename T>
class ArenaVector
{
public:
    ArenaVector(T* buffer, size_t capacity)
        : buffer(buffer), count(0), capacity(capacity)
    {
        assert(buffer != nullptr);
        assert(capacity > 0);
    }

    void PushBack(const T& item)
    {
        assert(count < capacity && "ArenaVector overflow");
        buffer[count++] = item;
    }

    void PushBack(T&& item)
    {
        assert(count < capacity && "ArenaVector overflow");
        buffer[count++] = std::move(item);
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args)
    {
        assert(count < capacity && "ArenaVector overflow");
        buffer[count] = T{std::forward<Args>(args)...};
        return buffer[count++];
    }

    void PopBack()
    {
        assert(count > 0 && "ArenaVector underflow");
        count--;
    }

    T PopBackValue()
    {
        assert(count > 0 && "ArenaVector underflow");
        return std::move(buffer[--count]);
    }

    void Clear() { count = 0; }

    /**
     * O(n) removal, preserves order.
     */
    void RemoveAt(size_t index)
    {
        assert(index < count && "Index out of bounds");
        for (size_t i = index; i < count - 1; ++i) {
            buffer[i] = std::move(buffer[i + 1]);
        }
        count--;
    }

    /**
     * O(1) removal, does not preserve order.
     * Swaps target with last element then pops.
     */
    void SwapRemove(size_t index)
    {
        assert(index < count && "Index out of bounds");
        if (index != count - 1) {
            buffer[index] = std::move(buffer[count - 1]);
        }
        count--;
    }

    T& operator[](size_t index) { assert(index < count); return buffer[index]; }
    const T& operator[](size_t index) const { assert(index < count); return buffer[index]; }

    T& Front() { assert(count > 0); return buffer[0]; }
    const T& Front() const { assert(count > 0); return buffer[0]; }

    T& Back() { assert(count > 0); return buffer[count - 1]; }
    const T& Back() const { assert(count > 0); return buffer[count - 1]; }

    T* Data() { return buffer; }
    const T* Data() const { return buffer; }

    size_t Size() const { return count; }
    size_t GetCapacity() const { return capacity; }
    bool IsEmpty() const { return count == 0; }
    bool IsFull() const { return count >= capacity; }

    T* begin() { return buffer; }
    T* end() { return buffer + count; }
    const T* begin() const { return buffer; }
    const T* end() const { return buffer + count; }

private:
    T* buffer;
    size_t count;
    size_t capacity;
};
} // Core

#endif //WILL_ENGINE_FIXED_VECTOR_H
