//
// Created by William on 2026-04-06.
//

#ifndef WILL_ENGINE_HEAP_ARRAY_H
#define WILL_ENGINE_HEAP_ARRAY_H

#include <cassert>
#include <cstddef>
#include <new>
#include <utility>

#include "core/memory/tlsf_allocator.h"

namespace Core
{
/**
 * Fixed-size array with runtime capacity backed by a TlsfAllocator.
 * All N slots are always live (constructed at init, destroyed at reset).
 * Size equals capacity; there is no push/pop interface.
 *
 * PREFER this over FixedVector when all slots are always valid.
 * Use FixedVector when only a subset of slots are occupied at a time.
 */
template<typename T>
class HeapArray
{
public:
    HeapArray() = default;

    HeapArray(TlsfAllocator* alloc, AllocTag tag, size_t size)
        : alloc_(alloc), tag_(tag), size_(size)
    {
        assert(alloc_ != nullptr && "HeapArray: no allocator");
        assert(size_ > 0 && "HeapArray: size must be > 0");
        data_ = static_cast<T*>(alloc_->Alloc(size_ * sizeof(T), tag_));
        assert(data_ != nullptr && "HeapArray: allocation failed");
        for (size_t i = 0; i < size_; ++i) {
            new(data_ + i) T();
        }
    }

    ~HeapArray() { Reset(); }

    HeapArray(const HeapArray& other)
        : alloc_(other.alloc_), tag_(other.tag_), size_(other.size_)
    {
        if (other.data_) {
            data_ = static_cast<T*>(alloc_->Alloc(size_ * sizeof(T), tag_));
            assert(data_ != nullptr && "HeapArray: allocation failed");
            for (size_t i = 0; i < size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
        }
    }

    HeapArray& operator=(const HeapArray& other)
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_ = other.alloc_;
        tag_   = other.tag_;
        size_  = other.size_;
        if (other.data_) {
            data_ = static_cast<T*>(alloc_->Alloc(size_ * sizeof(T), tag_));
            assert(data_ != nullptr && "HeapArray: allocation failed");
            for (size_t i = 0; i < size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
        }
        return *this;
    }

    HeapArray(HeapArray&& other) noexcept
        : alloc_(other.alloc_), tag_(other.tag_),
          data_(other.data_), size_(other.size_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    HeapArray& operator=(HeapArray&& other) noexcept
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_      = other.alloc_;
        tag_        = other.tag_;
        data_       = other.data_;
        size_       = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
        return *this;
    }

    void Reset()
    {
        if (data_) {
            for (size_t i = 0; i < size_; ++i) { data_[i].~T(); }
            alloc_->Free(data_);
            data_ = nullptr;
            size_ = 0;
        }
    }

    T&       operator[](size_t i)       { assert(i < size_); return data_[i]; }
    const T& operator[](size_t i) const { assert(i < size_); return data_[i]; }

    T&       Front()       { assert(size_ > 0); return data_[0]; }
    const T& Front() const { assert(size_ > 0); return data_[0]; }
    T&       Back()        { assert(size_ > 0); return data_[size_ - 1]; }
    const T& Back()  const { assert(size_ > 0); return data_[size_ - 1]; }

    T*       Data()       { return data_; }
    const T* Data() const { return data_; }

    size_t Size()        const { return size_; }
    bool   IsEmpty()     const { return size_ == 0; }
    bool   IsAllocated() const { return data_ != nullptr; }

    T*       begin()       { return data_; }
    T*       end()         { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end()   const { return data_ + size_; }

private:
    TlsfAllocator* alloc_{};
    AllocTag       tag_{AllocTag::Unknown};
    T*             data_{};
    size_t         size_{};
};
} // namespace Core

#endif // WILL_ENGINE_HEAP_ARRAY_H
