//
// Created by William on 2026-05-08.
//

#ifndef WILL_ENGINE_INLINE_QUEUE_H
#define WILL_ENGINE_INLINE_QUEUE_H

#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>

namespace Core
{
/**
 * FIFO queue with compile-time capacity. Storage is inline (embedded in the object itself),
 * so no allocator is needed. Asserts on overflow.
 *
 * PREFER this over a general queue when the maximum count is a known compile-time constant.
 */
template<typename T, size_t N>
class InlineQueue
{
    static_assert(N > 0, "InlineQueue: N must be > 0");

public:
    InlineQueue() = default;

    void Push(const T& item)
    {
        assert(size_ < N && "InlineQueue overflow");
        data_[(head_ + size_) % N] = item;
        ++size_;
    }

    void Push(T&& item)
    {
        assert(size_ < N && "InlineQueue overflow");
        data_[(head_ + size_) % N] = std::move(item);
        ++size_;
    }

    T Pop()
    {
        assert(size_ > 0 && "InlineQueue underflow");
        T val = std::move(data_[head_]);
        head_ = (head_ + 1) % N;
        --size_;
        return val;
    }

    std::optional<T> TryPop()
    {
        if (size_ == 0) { return std::nullopt; }
        return Pop();
    }

    T& Front()
    {
        assert(size_ > 0 && "InlineQueue is empty");
        return data_[head_];
    }

    const T& Front() const
    {
        assert(size_ > 0 && "InlineQueue is empty");
        return data_[head_];
    }

    void Clear()
    {
        head_ = 0;
        size_ = 0;
    }

    [[nodiscard]] size_t Size() const { return size_; }
    [[nodiscard]] constexpr size_t GetCapacity() const { return N; }
    [[nodiscard]] bool IsEmpty() const { return size_ == 0; }
    [[nodiscard]] bool IsFull() const { return size_ >= N; }

private:
    T data_[N]{};
    size_t head_{0};
    size_t size_{0};
};
} // namespace Core

#endif // WILL_ENGINE_INLINE_QUEUE_H
