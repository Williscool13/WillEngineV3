//
// Created by William on 2026-04-09.
//

#ifndef WILL_ENGINE_ARENA_VECTOR_H
#define WILL_ENGINE_ARENA_VECTOR_H

#include <cassert>
#include <cstddef>
#include <cstring>
#include <new>
#include <utility>

#include "core/memory/arena.h"

namespace Core
{
/**
 * Dynamically-resizing vector backed by an Arena.
 * Grows by allocating a new block from the arena and copying; the old block is abandoned
 * until the arena is bulk-reset externally. Clear() calls element destructors but does NOT
 * free memory. Reset() additionally forgets the allocation.
 *
 * Suitable for per-frame or per-scope scratch data where the arena outlives the vector.
 * PREFER ArenaFixedVector when the maximum count is known at construction time.
 */
template<typename T>
class ArenaVector
{
public:
    ArenaVector() = default;

    explicit ArenaVector(Arena* arena)
        : arena_(arena)
    {}

    ArenaVector(Arena* arena, size_t initialCapacity)
        : arena_(arena)
    {
        if (initialCapacity > 0) {
            Reserve(initialCapacity);
        }
    }

    ~ArenaVector() { Clear(); }

    ArenaVector(const ArenaVector& other)
        : arena_(other.arena_)
    {
        if (other.size_ > 0) {
            Reserve(other.size_);
            for (size_t i = 0; i < other.size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
            size_ = other.size_;
        }
    }

    ArenaVector& operator=(const ArenaVector& other)
    {
        if (this == &other) { return *this; }
        Clear();
        arena_ = other.arena_;
        if (other.size_ > 0) {
            Reserve(other.size_);
            for (size_t i = 0; i < other.size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
            size_ = other.size_;
        }
        return *this;
    }

    ArenaVector(ArenaVector&& other) noexcept
        : arena_(other.arena_),
          data_(other.data_), size_(other.size_), capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    ArenaVector& operator=(ArenaVector&& other) noexcept
    {
        if (this == &other) { return *this; }
        Clear();
        arena_ = other.arena_;
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
        assert(size_ > 0 && "ArenaVector underflow");
        --size_;
        data_[size_].~T();
    }

    T PopBackValue()
    {
        assert(size_ > 0 && "ArenaVector underflow");
        --size_;
        T val = std::move(data_[size_]);
        data_[size_].~T();
        return val;
    }

    // O(n) linear scan
    bool Contains(const T& value) const
    {
        for (size_t i = 0; i < size_; ++i) {
            if (data_[i] == value) { return true; }
        }
        return false;
    }

    bool RemoveFirst(const T& value)
    {
        for (size_t i = 0; i < size_; ++i) {
            if (data_[i] == value) {
                RemoveAt(i);
                return true;
            }
        }
        return false;
    }

    template<typename Pred>
    bool RemoveFirstIf(Pred&& pred)
    {
        for (size_t i = 0; i < size_; ++i) {
            if (pred(data_[i])) {
                RemoveAt(i);
                return true;
            }
        }
        return false;
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

    T* Remove(T* it)
    {
        assert(it >= data_ && it < data_ + size_);
        size_t index = static_cast<size_t>(it - data_);
        RemoveAt(index);
        return data_ + index;
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
        assert(arena_ != nullptr && "ArenaVector: no arena");
        T* newData = static_cast<T*>(arena_->AllocRaw(newCapacity * sizeof(T), alignof(T)));
        assert(newData != nullptr && "ArenaVector: allocation failed");
        if (data_) {
            for (size_t i = 0; i < size_; ++i) {
                new(newData + i) T(std::move(data_[i]));
                data_[i].~T();
            }
        }
        data_ = newData;
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
        if (size_ + count > capacity_) { Grow(size_ + count); }
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
        if (size_ + count > capacity_) { Grow(size_ + count); }
        for (size_t i = size_; i > index; --i) {
            new(data_ + i + count - 1) T(std::move(data_[i - 1]));
            data_[i - 1].~T();
        }
        for (size_t i = 0; i < count; ++i) {
            new(data_ + index + i) T(first[i]);
        }
        size_ += count;
    }

    void Clear()
    {
        for (size_t i = 0; i < size_; ++i) { data_[i].~T(); }
        size_ = 0;
    }

    // Calls destructors and forgets the allocation. Arena memory is reclaimed on arena reset.
    void Reset()
    {
        Clear();
        data_ = nullptr;
        capacity_ = 0;
    }

    bool operator==(const ArenaVector& other) const
    {
        if (size_ != other.size_) { return false; }
        for (size_t i = 0; i < size_; ++i) {
            if (!(data_[i] == other.data_[i])) { return false; }
        }
        return true;
    }

    bool operator!=(const ArenaVector& other) const { return !(*this == other); }

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

    T& Front() { assert(size_ > 0); return data_[0]; }
    const T& Front() const { assert(size_ > 0); return data_[0]; }
    T& Back() { assert(size_ > 0); return data_[size_ - 1]; }
    const T& Back() const { assert(size_ > 0); return data_[size_ - 1]; }

    T* Data() { return data_; }
    const T* Data() const { return data_; }

    [[nodiscard]] size_t Size()        const { return size_; }
    [[nodiscard]] size_t GetCapacity() const { return capacity_; }
    [[nodiscard]] bool   IsEmpty()     const { return size_ == 0; }
    [[nodiscard]] bool   IsFull()      const { return size_ >= capacity_; }
    [[nodiscard]] bool   IsAllocated() const { return data_ != nullptr; }

    T* begin() { return data_; }
    T* end()   { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end()   const { return data_ + size_; }

private:
    void Grow(size_t minCapacity)
    {
        size_t newCapacity = capacity_ < 4 ? 4 : capacity_ * 2;
        if (newCapacity < minCapacity) { newCapacity = minCapacity; }
        Reserve(newCapacity);
    }

    Arena* arena_{};
    T* data_{};
    size_t size_{};
    size_t capacity_{};
};
} // namespace Core

#endif // WILL_ENGINE_ARENA_VECTOR_H
