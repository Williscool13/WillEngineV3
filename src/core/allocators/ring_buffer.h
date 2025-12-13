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
public:
    RingBuffer()
        : head(0), tail(0)
    {}

    bool Push(const T& item)
    {
        if (IsFull()) return false;
        buffer[tail] = item;
        tail = (tail + 1) % Capacity;
        return true;
    }

    bool Push(T&& item)
    {
        if (IsFull()) return false;
        buffer[tail] = std::move(item);
        tail = (tail + 1) % Capacity;
        return true;
    }

    template<typename... Args>
    bool Emplace(Args&&... args)
    {
        if (IsFull()) return false;
        buffer[tail] = T(std::forward<Args>(args)...);
        tail = (tail + 1) % Capacity;
        return true;
    }

    bool Pop(T& item)
    {
        if (IsEmpty()) return false;
        item = std::move(buffer[head]);
        head = (head + 1) % Capacity;

        return true;
    }

    void Clear()
    {
        head = 0;
        tail = 0;
    }

    [[nodiscard]] constexpr size_t GetCapacity() const { return Capacity; }
    [[nodiscard]] bool IsEmpty() const { return head == tail; }
    [[nodiscard]] bool IsFull() const { return (tail + 1) % Capacity == head; }
    [[nodiscard]] size_t GetSize() const { return head <= tail ? tail - head : Capacity - head + tail; }

private:
    std::array<T, Capacity> buffer;
    size_t head;
    size_t tail;
};
}


#endif //WILLENGINETESTBED_RING_BUFFER_H
