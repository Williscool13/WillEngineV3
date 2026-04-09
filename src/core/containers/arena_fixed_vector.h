//
// Created by William on 2026-04-01.
//

#ifndef WILL_ENGINE_ARENA_FIXED_VECTOR_H
#define WILL_ENGINE_ARENA_FIXED_VECTOR_H

#include <cassert>
#include <cstddef>
#include <new>
#include <utility>

#include "core/memory/arena.h"

namespace Core
{
/**
 * Non-resizing vector with runtime capacity backed by an Arena.
 * Capacity is set at construction and never changes. Asserts on overflow.
 * Clear() calls element destructors. Does NOT free memory; the arena is bulk-reset externally.
 *
 * PREFER this over ArenaFixedMap for ordered, index-accessible scratch data.
 * Use only with arenas that outlive the container (e.g. per-frame render arena).
 */
template<typename T>
class ArenaFixedVector
{
public:
    ArenaFixedVector() = default;

    ArenaFixedVector(Arena* arena, size_t capacity)
        : arena_(arena), capacity_(capacity)
    {
        assert(arena_ != nullptr && "ArenaFixedVector: no arena");
        assert(capacity_ > 0 && "ArenaFixedVector: capacity must be > 0");
        data_ = static_cast<T*>(arena_->AllocRaw(capacity_ * sizeof(T), alignof(T)));
        assert(data_ != nullptr && "ArenaFixedVector: allocation failed");
    }

    ~ArenaFixedVector() { Clear(); }

    ArenaFixedVector(const ArenaFixedVector& other)
        : arena_(other.arena_), capacity_(other.capacity_)
    {
        if (other.data_) {
            data_ = static_cast<T*>(arena_->AllocRaw(capacity_ * sizeof(T), alignof(T)));
            assert(data_ != nullptr && "ArenaFixedVector: allocation failed");
            for (size_t i = 0; i < other.size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
            size_ = other.size_;
        }
    }

    ArenaFixedVector& operator=(const ArenaFixedVector& other)
    {
        if (this == &other) { return *this; }
        Clear();
        arena_ = other.arena_;
        capacity_ = other.capacity_;
        if (other.data_) {
            data_ = static_cast<T*>(arena_->AllocRaw(capacity_ * sizeof(T), alignof(T)));
            assert(data_ != nullptr && "ArenaFixedVector: allocation failed");
            for (size_t i = 0; i < other.size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
            size_ = other.size_;
        }
        return *this;
    }

    ArenaFixedVector(ArenaFixedVector&& other) noexcept
        : arena_(other.arena_),
          data_(other.data_), size_(other.size_), capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    ArenaFixedVector& operator=(ArenaFixedVector&& other) noexcept
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
        assert(size_ < capacity_ && "ArenaFixedVector overflow");
        new(data_ + size_) T(item);
        ++size_;
    }

    void PushBack(T&& item)
    {
        assert(size_ < capacity_ && "ArenaFixedVector overflow");
        new(data_ + size_) T(std::move(item));
        ++size_;
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args)
    {
        assert(size_ < capacity_ && "ArenaFixedVector overflow");
        T* p = new(data_ + size_) T(std::forward<Args>(args)...);
        ++size_;
        return *p;
    }

    void PopBack()
    {
        assert(size_ > 0 && "ArenaFixedVector underflow");
        --size_;
        data_[size_].~T();
    }

    T PopBackValue()
    {
        assert(size_ > 0 && "ArenaFixedVector underflow");
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
    size_t GetCapacity() const { return capacity_; }
    bool   IsEmpty() const { return size_ == 0; }
    bool   IsFull() const { return size_ >= capacity_; }
    bool   IsAllocated() const { return data_ != nullptr; }

    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

private:
    Arena* arena_{};
    T* data_{};
    size_t size_{};
    size_t capacity_{};
};
} // namespace Core

#endif // WILL_ENGINE_ARENA_FIXED_VECTOR_H
