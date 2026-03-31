//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_INLINE_VECTOR_H
#define WILL_ENGINE_INLINE_VECTOR_H

#include <cassert>
#include <cstddef>
#include <utility>

namespace Core
{
/**
 * Non-resizing vector with compile-time capacity. Storage is inline (embedded in the object itself),
 * so no allocator is needed. The object lives wherever it is declared.
 *
 * PREFER this over FixedVector or Vector when the maximum count is a known compile-time constant.
 * Use FixedVector when the maximum count is only known at runtime.
 * Use Vector as a last resort for truly unbounded growth.
 */
template<typename T, size_t N>
class InlineVector
{
    static_assert(N > 0, "InlineVector: N must be > 0");

public:
    InlineVector() = default;

    void PushBack(const T& item)
    {
        assert(size_ < N && "InlineVector overflow");
        data_[size_++] = item;
    }

    void PushBack(T&& item)
    {
        assert(size_ < N && "InlineVector overflow");
        data_[size_++] = std::move(item);
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args)
    {
        assert(size_ < N && "InlineVector overflow");
        data_[size_] = T{std::forward<Args>(args)...};
        return data_[size_++];
    }

    void PopBack()
    {
        assert(size_ > 0 && "InlineVector underflow");
        --size_;
    }

    T PopBackValue()
    {
        assert(size_ > 0 && "InlineVector underflow");
        return std::move(data_[--size_]);
    }

    void RemoveAt(size_t index)
    {
        assert(index < size_ && "Index out of bounds");
        for (size_t i = index; i < size_ - 1; ++i) {
            data_[i] = std::move(data_[i + 1]);
        }
        --size_;
    }

    void SwapRemove(size_t index)
    {
        assert(index < size_ && "Index out of bounds");
        if (index != size_ - 1) {
            data_[index] = std::move(data_[size_ - 1]);
        }
        --size_;
    }

    void Clear() { size_ = 0; }

    T& operator[](size_t i)
    {
        assert(i < size_);
        return data_[i];
    }

    const T& operator[](size_t i) const
    {
        assert(i < size_);
        return data_[i];
    }

    T& Front()
    {
        assert(size_ > 0);
        return data_[0];
    }

    const T& Front() const
    {
        assert(size_ > 0);
        return data_[0];
    }

    T& Back()
    {
        assert(size_ > 0);
        return data_[size_ - 1];
    }

    const T& Back() const
    {
        assert(size_ > 0);
        return data_[size_ - 1];
    }

    T* Data() { return data_; }
    const T* Data() const { return data_; }

    size_t Size() const { return size_; }
    constexpr size_t GetCapacity() const { return N; }
    bool IsEmpty() const { return size_ == 0; }
    bool IsFull() const { return size_ >= N; }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

private:
    T data_[N]{};
    size_t size_{};
};
} // namespace Core

#endif // WILL_ENGINE_INLINE_VECTOR_H
