//
// Created by William on 2026-05-08.
//

#ifndef WILL_ENGINE_ARENA_QUEUE_H
#define WILL_ENGINE_ARENA_QUEUE_H

#include <cassert>
#include <cstddef>
#include <new>
#include <optional>
#include <utility>

#include "core/memory/arena.h"

namespace Core
{
/**
 * FIFO queue backed by an Arena. Grows by allocating a new block from the arena and copying;
 * the old block is abandoned until the arena is bulk-reset externally.
 * Clear() calls element destructors but does NOT free memory.
 * Reset() additionally forgets the allocation.
 *
 * Suitable for per-frame or per-scope scratch data where the arena outlives the queue.
 * PREFER InlineQueue when the maximum count is a known compile-time constant.
 */
template<typename T>
class ArenaQueue
{
public:
    ArenaQueue() = default;

    explicit ArenaQueue(Arena* arena)
        : arena_(arena)
    {}

    ArenaQueue(Arena* arena, size_t initialCapacity)
        : arena_(arena)
    {
        if (initialCapacity > 0) {
            Reserve(initialCapacity);
        }
    }

    ~ArenaQueue() { Clear(); }

    ArenaQueue(const ArenaQueue&) = delete;
    ArenaQueue& operator=(const ArenaQueue&) = delete;

    ArenaQueue(ArenaQueue&& other) noexcept
        : arena_(other.arena_),
          data_(other.data_), head_(other.head_), size_(other.size_), capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.head_ = 0;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    ArenaQueue& operator=(ArenaQueue&& other) noexcept
    {
        if (this == &other) { return *this; }
        Clear();
        arena_ = other.arena_;
        data_ = other.data_;
        head_ = other.head_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.head_ = 0;
        other.size_ = 0;
        other.capacity_ = 0;
        return *this;
    }

    void Push(const T& item)
    {
        if (size_ == capacity_) { Grow(); }
        size_t tail = (head_ + size_) % capacity_;
        new(data_ + tail) T(item);
        ++size_;
    }

    void Push(T&& item)
    {
        if (size_ == capacity_) { Grow(); }
        size_t tail = (head_ + size_) % capacity_;
        new(data_ + tail) T(std::move(item));
        ++size_;
    }

    template<typename... Args>
    T& Emplace(Args&&... args)
    {
        if (size_ == capacity_) { Grow(); }
        size_t tail = (head_ + size_) % capacity_;
        T* p = new(data_ + tail) T(std::forward<Args>(args)...);
        ++size_;
        return *p;
    }

    T Pop()
    {
        assert(size_ > 0 && "ArenaQueue underflow");
        T val = std::move(data_[head_]);
        data_[head_].~T();
        head_ = (head_ + 1) % capacity_;
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
        assert(size_ > 0 && "ArenaQueue is empty");
        return data_[head_];
    }

    const T& Front() const
    {
        assert(size_ > 0 && "ArenaQueue is empty");
        return data_[head_];
    }

    void Reserve(size_t newCapacity)
    {
        if (newCapacity <= capacity_) { return; }
        assert(arena_ != nullptr && "ArenaQueue: no arena");
        T* newData = static_cast<T*>(arena_->AllocRaw(newCapacity * sizeof(T), alignof(T)));
        assert(newData != nullptr && "ArenaQueue: allocation failed");
        for (size_t i = 0; i < size_; ++i) {
            size_t src = (head_ + i) % capacity_;
            new(newData + i) T(std::move(data_[src]));
            data_[src].~T();
        }
        data_ = newData;
        head_ = 0;
        capacity_ = newCapacity;
    }

    void Clear()
    {
        for (size_t i = 0; i < size_; ++i) {
            data_[(head_ + i) % capacity_].~T();
        }
        head_ = 0;
        size_ = 0;
    }

    // Calls destructors and forgets the allocation. Arena memory is reclaimed on arena reset.
    void Reset()
    {
        Clear();
        data_ = nullptr;
        capacity_ = 0;
    }

    [[nodiscard]] size_t Size()        const { return size_; }
    [[nodiscard]] size_t GetCapacity() const { return capacity_; }
    [[nodiscard]] bool   IsEmpty()     const { return size_ == 0; }
    [[nodiscard]] bool   IsFull()      const { return size_ >= capacity_; }
    [[nodiscard]] bool   IsAllocated() const { return data_ != nullptr; }

private:
    void Grow()
    {
        size_t newCapacity = capacity_ < 4 ? 4 : capacity_ * 2;
        Reserve(newCapacity);
    }

    Arena* arena_{};
    T* data_{};
    size_t head_{0};
    size_t size_{0};
    size_t capacity_{0};
};
} // namespace Core

#endif // WILL_ENGINE_ARENA_QUEUE_H
