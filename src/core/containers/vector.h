//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_VECTOR_H
#define WILL_ENGINE_VECTOR_H

#include <cassert>
#include <cstddef>
#include <cstring>
#include <new>
#include <utility>

#include "core/memory/tlsf_allocator.h"

namespace Core
{
/**
 * Dynamic array backed by a TlsfAllocator.
 * Grows by doubling (minimum capacity 4).
 * Elements are placement-new'd; non-trivial destructors are called on removal.
 *
 * PREFER StackVector or InlineVector wherever the maximum count is known at init time.
 * Vector is a last resort for truly unbounded, unpredictable growth.
 */
template<typename T>
class Vector
{
public:
    Vector() = default;

    explicit Vector(TlsfAllocator* alloc, AllocTag tag = AllocTag::Unknown)
        : alloc_(alloc), tag_(tag)
    {}

    Vector(TlsfAllocator* alloc, AllocTag tag, size_t initialCapacity)
        : alloc_(alloc), tag_(tag)
    {
        if (initialCapacity > 0) {
            Reserve(initialCapacity);
        }
    }

    ~Vector() { Reset(); }

    Vector(const Vector& other)
        : alloc_(other.alloc_), tag_(other.tag_)
    {
        if (other.size_ > 0) {
            Reserve(other.size_);
            for (size_t i = 0; i < other.size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
            size_ = other.size_;
        }
    }

    Vector& operator=(const Vector& other)
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_ = other.alloc_;
        tag_ = other.tag_;
        if (other.size_ > 0) {
            Reserve(other.size_);
            for (size_t i = 0; i < other.size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
            size_ = other.size_;
        }
        return *this;
    }

    Vector(Vector&& other) noexcept
        : alloc_(other.alloc_), tag_(other.tag_),
          data_(other.data_), size_(other.size_), capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    Vector& operator=(Vector&& other) noexcept
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_ = other.alloc_;
        tag_ = other.tag_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        return *this;
    }

    void PushBack(const T& item)
    {
        if (size_ == capacity_) { Grow(size_ + 1); }
        new(data_ + size_) T(item);
        ++size_;
    }

    void PushBack(T&& item)
    {
        if (size_ == capacity_) { Grow(size_ + 1); }
        new(data_ + size_) T(std::move(item));
        ++size_;
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args)
    {
        if (size_ == capacity_) { Grow(size_ + 1); }
        T* p = new(data_ + size_) T(std::forward<Args>(args)...);
        ++size_;
        return *p;
    }

    void PopBack()
    {
        assert(size_ > 0 && "Vector underflow");
        --size_;
        data_[size_].~T();
    }

    T PopBackValue()
    {
        assert(size_ > 0 && "Vector underflow");
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

    void Reserve(size_t newCapacity)
    {
        if (newCapacity <= capacity_) { return; }
        assert(alloc_ != nullptr && "Vector: no allocator");
        void* raw;
        if (data_) {
            raw = alloc_->Realloc(data_, newCapacity * sizeof(T), tag_);
        }
        else {
            raw = alloc_->Alloc(newCapacity * sizeof(T), tag_);
        }
        assert(raw != nullptr && "Vector: allocation failed");
        data_ = static_cast<T*>(raw);
        capacity_ = newCapacity;
    }

    void Resize(size_t newSize)
    {
        if (newSize > capacity_) { Reserve(newSize); }
        for (size_t i = size_; i < newSize; ++i) {
            new(data_ + i) T();
        }
        for (size_t i = newSize; i < size_; ++i) {
            data_[i].~T();
        }
        size_ = newSize;
    }

    void Append(const T* first, const T* last)
    {
        size_t count = static_cast<size_t>(last - first);
        if (count == 0) { return; }

        if (size_ + count > capacity_) {
            Grow(size_ + count);
        }

        for (size_t i = 0; i < count; ++i) {
            new(data_ + size_ + i) T(first[i]);
        }
        size_ += count;
    }

    void Insert(T* pos, const T* first, const T* last)
    {
        assert(pos >= data_ && pos <= data_ + size_);
        size_t index = static_cast<size_t>(pos - data_);
        size_t count = static_cast<size_t>(last - first);
        if (count == 0) { return; }

        if (size_ + count > capacity_) {
            Grow(size_ + count);
        }

        // Shift existing elements
        for (size_t i = size_; i > index; --i) {
            new(data_ + i + count - 1) T(std::move(data_[i - 1]));
            data_[i - 1].~T();
        }

        // Copy-construct new elements
        for (size_t i = 0; i < count; ++i) {
            new(data_ + index + i) T(first[i]);
        }
        size_ += count;
    }

    void Clear()
    {
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
        size_ = 0;
    }

    void Reset()
    {
        Clear();
        if (data_) {
            alloc_->Free(data_);
            data_ = nullptr;
            capacity_ = 0;
        }
    }

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

    [[nodiscard]] size_t Size() const { return size_; }
    [[nodiscard]] size_t GetCapacity() const { return capacity_; }
    [[nodiscard]] bool IsEmpty() const { return size_ == 0; }
    [[nodiscard]] bool IsAllocated() const { return data_ != nullptr; }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

private:
    void Grow(size_t minCapacity)
    {
        size_t newCapacity = capacity_ < 4 ? 4 : capacity_ * 2;
        if (newCapacity < minCapacity) { newCapacity = minCapacity; }
        Reserve(newCapacity);
    }

    TlsfAllocator* alloc_{};
    AllocTag tag_{AllocTag::Unknown};
    T* data_{};
    size_t size_{};
    size_t capacity_{};
};
} // namespace Core

#endif // WILL_ENGINE_VECTOR_H
