//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_ARRAY_H
#define WILL_ENGINE_ARRAY_H

#include <cassert>
#include <cstddef>
#include <initializer_list>

namespace Core
{
/**
 * Fixed-size array with compile-time capacity. Storage is inline (embedded in the object),
 * so no allocator is needed. All N slots are always live.
 * Use when size is a compile-time constant and all elements are always valid.
 */
template<typename T, size_t N>
class Array
{
    static_assert(N > 0, "Array size must be > 0");

public:
    constexpr Array() = default;

    constexpr Array(std::initializer_list<T> init)
    {
        assert(init.size() <= N && "Initializer list exceeds Array capacity");
        size_t i = 0;
        for (const T& v : init) {
            data_[i++] = v;
        }
    }

    T& operator[](size_t i)
    {
        assert(i < N);
        return data_[i];
    }

    const T& operator[](size_t i) const
    {
        assert(i < N);
        return data_[i];
    }

    T& Front() { return data_[0]; }
    const T& Front() const { return data_[0]; }
    T& Back() { return data_[N - 1]; }
    const T& Back() const { return data_[N - 1]; }

    T* Data() { return data_; }
    const T* Data() const { return data_; }

    constexpr size_t Size() const { return N; }

    T* begin() { return data_; }
    T* end() { return data_ + N; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + N; }

private:
    T data_[N]{};
};
} // namespace Core

#endif // WILL_ENGINE_ARRAY_H
