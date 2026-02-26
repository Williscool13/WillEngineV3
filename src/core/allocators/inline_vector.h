//
// Created by William on 2026-02-26.
//

#ifndef WILLENGINE_INLINE_VECTOR_H
#define WILLENGINE_INLINE_VECTOR_H

#include <array>
#include <cstddef>
#include <cassert>

namespace Core
{
template<typename T, size_t Capacity>
class InlineVector
{
    static_assert(Capacity > 0, "Capacity must be greater than 0");

public:
    InlineVector()
        : count(0)
    {}

    void PushBack(const T& item)
    {
        assert(count < Capacity && "InlineVector overflow");
        buffer[count++] = item;
    }

    void PushBack(T&& item)
    {
        assert(count < Capacity && "InlineVector overflow");
        buffer[count++] = std::move(item);
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args)
    {
        assert(count < Capacity && "InlineVector overflow");
        buffer[count] = T{std::forward<Args>(args)...};
        return buffer[count++];
    }

    void PopBack()
    {
        assert(count > 0 && "InlineVector underflow");
        count--;
    }

    T PopBackValue()
    {
        assert(count > 0 && "InlineVector underflow");
        return std::move(buffer[--count]);
    }

    void Clear() { count = 0; }

    /**
     * O(n) removal, preserves order
     */
    void RemoveAt(size_t index)
    {
        assert(index < count && "Index out of bounds");
        for (size_t i = index; i < count - 1; i++) {
            buffer[i] = std::move(buffer[i + 1]);
        }
        count--;
    }

    /**
     * O(1) removal, does not preserve order
     * Swaps target with last element then pops
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

    T* Data() { return buffer.data(); }
    const T* Data() const { return buffer.data(); }

    size_t Size() const { return count; }
    constexpr size_t GetCapacity() const { return Capacity; }
    bool IsEmpty() const { return count == 0; }
    bool IsFull() const { return count >= Capacity; }

    T* begin() { return buffer.data(); }
    T* end() { return buffer.data() + count; }
    const T* begin() const { return buffer.data(); }
    const T* end() const { return buffer.data() + count; }

private:
    std::array<T, Capacity> buffer;
    size_t count;
};
}

#endif //WILLENGINE_INLINE_VECTOR_H