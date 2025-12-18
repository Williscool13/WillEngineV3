//
// Created by William on 2025-11-14.
//

#ifndef WILLENGINETESTBED_RING_BUFFER_H
#define WILLENGINETESTBED_RING_BUFFER_H

#include <array>
#include <cstddef>

namespace Core
{
template<typename T, size_t Capacity>
class RingBuffer
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity > 0, "Capacity must be greater than 0");

public:
    RingBuffer()
        : head(0)
        , tail(0)
    {}

    void Push(const T& item)
    {
        buffer[tail & Mask] = item;
        tail++;
    }

    bool Pop(T& item)
    {
        if (IsEmpty()) return false;

        item = buffer[head & Mask];
        head++;
        return true;
    }

    void Clear()
    {
        head = 0;
        tail = 0;
    }

    size_t GetSize() const { return tail - head; }
    constexpr size_t GetCapacity() const { return Capacity; }
    bool IsEmpty() const { return head == tail; }
    bool IsFull() const { return (tail - head) >= Capacity; }

private:
    static constexpr size_t Mask = Capacity - 1;
    std::array<T, Capacity> buffer;
    size_t head;
    size_t tail;
};
}


#endif //WILLENGINETESTBED_RING_BUFFER_H
