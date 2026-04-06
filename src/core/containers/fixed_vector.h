//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_FIXED_VECTOR_H
#define WILL_ENGINE_FIXED_VECTOR_H

#include <cassert>
#include <cstddef>
#include <new>
#include <utility>

#include "core/memory/tlsf_allocator.h"

namespace Core
{
/**
 * Non-resizing vector with runtime capacity backed by a TlsfAllocator.
 * Capacity is set at construction and never changes. Asserts on overflow.
 *
 * PREFER this over Vector when the maximum count is known at init time but not compile time.
 * Use InlineVector when capacity is a compile-time constant.
 * Use Vector as a last resort for truly unbounded growth.
 */
template<typename T>
class FixedVector
{
public:
    FixedVector() = default;

    FixedVector(TlsfAllocator* alloc, AllocTag tag, size_t capacity)
        : alloc_(alloc), tag_(tag), capacity_(capacity)
    {
        assert(alloc_ != nullptr && "FixedVector: no allocator");
        assert(capacity_ > 0 && "FixedVector: capacity must be > 0");
        data_ = static_cast<T*>(alloc_->Alloc(capacity_ * sizeof(T), tag_));
        assert(data_ != nullptr && "FixedVector: allocation failed");
    }

    ~FixedVector() { Reset(); }

    FixedVector(const FixedVector& other)
        : alloc_(other.alloc_), tag_(other.tag_), capacity_(other.capacity_)
    {
        if (other.data_) {
            data_ = static_cast<T*>(alloc_->Alloc(capacity_ * sizeof(T), tag_));
            assert(data_ != nullptr && "FixedVector: allocation failed");
            for (size_t i = 0; i < other.size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
            size_ = other.size_;
        }
    }

    FixedVector& operator=(const FixedVector& other)
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_    = other.alloc_;
        tag_      = other.tag_;
        capacity_ = other.capacity_;
        if (other.data_) {
            data_ = static_cast<T*>(alloc_->Alloc(capacity_ * sizeof(T), tag_));
            assert(data_ != nullptr && "FixedVector: allocation failed");
            for (size_t i = 0; i < other.size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
            size_ = other.size_;
        }
        return *this;
    }

    FixedVector(FixedVector&& other) noexcept
        : alloc_(other.alloc_), tag_(other.tag_),
          data_(other.data_), size_(other.size_), capacity_(other.capacity_)
    {
        other.data_     = nullptr;
        other.size_     = 0;
        other.capacity_ = 0;
    }

    FixedVector& operator=(FixedVector&& other) noexcept
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_          = other.alloc_;
        tag_            = other.tag_;
        data_           = other.data_;
        size_           = other.size_;
        capacity_       = other.capacity_;
        other.data_     = nullptr;
        other.size_     = 0;
        other.capacity_ = 0;
        return *this;
    }

    void PushBack(const T& item)
    {
        assert(size_ < capacity_ && "FixedVector overflow");
        new(data_ + size_) T(item);
        ++size_;
    }

    void PushBack(T&& item)
    {
        assert(size_ < capacity_ && "FixedVector overflow");
        new(data_ + size_) T(std::move(item));
        ++size_;
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args)
    {
        assert(size_ < capacity_ && "FixedVector overflow");
        T* p = new(data_ + size_) T(std::forward<Args>(args)...);
        ++size_;
        return *p;
    }

    void PopBack()
    {
        assert(size_ > 0 && "FixedVector underflow");
        --size_;
        data_[size_].~T();
    }

    T PopBackValue()
    {
        assert(size_ > 0 && "FixedVector underflow");
        --size_;
        T val = std::move(data_[size_]);
        data_[size_].~T();
        return val;
    }

    void RemoveAt(size_t index)
    {
        assert(index < size_ && "Index out of bounds");
        data_[index].~T();
        for (size_t i = index; i < size_ - 1; ++i) {
            new(data_ + i) T(std::move(data_[i + 1]));
            data_[i + 1].~T();
        }
        --size_;
    }

    void SwapRemove(size_t index)
    {
        assert(index < size_ && "Index out of bounds");
        data_[index].~T();
        if (index != size_ - 1) {
            new(data_ + index) T(std::move(data_[size_ - 1]));
            data_[size_ - 1].~T();
        }
        --size_;
    }

    void Clear()
    {
        for (size_t i = 0; i < size_; ++i) { data_[i].~T(); }
        size_ = 0;
    }

    void Reset()
    {
        Clear();
        if (data_) {
            alloc_->Free(data_);
            data_     = nullptr;
            capacity_ = 0;
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
    size_t GetCapacity() const { return capacity_; }
    bool   IsEmpty()     const { return size_ == 0; }
    bool   IsFull()      const { return size_ >= capacity_; }
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
    size_t         capacity_{};
};
} // namespace Core

#endif // WILL_ENGINE_FIXED_VECTOR_H
