//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_SPAN_H
#define WILL_ENGINE_SPAN_H

#include <cassert>
#include <cstddef>

namespace Core
{
/**
 * Non-owning view over a contiguous sequence of T.
 *
 * Construct from a raw pointer + size, or from any contiguous container that exposes
 * Data() and Size() (Array, InlineVector, FixedVector, Vector).
 * Not applicable to map types.
 */
template<typename T>
class Span
{
public:
    constexpr Span() = default;

    constexpr Span(T* data, size_t size)
        : data_(data), size_(size)
    {}

    // Constructs from any contiguous container with Data() and Size().
    template<typename Container>
    constexpr explicit Span(Container& c)
        : data_(c.Data()), size_(c.Size())
    {}

    template<typename Container>
    constexpr explicit Span(const Container& c)
        : data_(c.Data()), size_(c.Size())
    {}

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
    bool IsEmpty() const { return size_ == 0; }

    Span Subspan(size_t offset, size_t count) const
    {
        assert(offset + count <= size_ && "Subspan out of range");
        return {data_ + offset, count};
    }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

private:
    T* data_{};
    size_t size_{};
};
} // namespace Core

#endif // WILL_ENGINE_SPAN_H
