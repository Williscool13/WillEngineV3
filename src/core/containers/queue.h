//
// Created by William on 2026-05-08.
//

#ifndef WILL_ENGINE_QUEUE_H
#define WILL_ENGINE_QUEUE_H

#include <cassert>
#include <cstddef>
#include <new>
#include <optional>
#include <utility>

#include "core/memory/tlsf_allocator.h"

namespace Core
{
/**
 * FIFO queue backed by a TlsfAllocator. Grows by doubling (minimum capacity 4).
 * Elements are placement-new'd; non-trivial destructors are called on removal.
 *
 * PREFER InlineQueue wherever the maximum count is a known compile-time constant.
 * Queue is for queues whose depth is unbounded or only known at runtime.
 */
template<typename T>
class Queue
{
public:
    Queue() = default;

    explicit Queue(TlsfAllocator* alloc, AllocTag tag = AllocTag::Unknown)
        : alloc_(alloc), tag_(tag)
    {}

    Queue(TlsfAllocator* alloc, AllocTag tag, size_t initialCapacity)
        : alloc_(alloc), tag_(tag)
    {
        if (initialCapacity > 0) {
            Reserve(initialCapacity);
        }
    }

    ~Queue() { Reset(); }

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    Queue(Queue&& other) noexcept
        : alloc_(other.alloc_), tag_(other.tag_),
          data_(other.data_), head_(other.head_), size_(other.size_), capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.head_ = 0;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    Queue& operator=(Queue&& other) noexcept
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_ = other.alloc_;
        tag_ = other.tag_;
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
        assert(size_ > 0 && "Queue underflow");
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
        assert(size_ > 0 && "Queue is empty");
        return data_[head_];
    }

    const T& Front() const
    {
        assert(size_ > 0 && "Queue is empty");
        return data_[head_];
    }

    void Reserve(size_t newCapacity)
    {
        if (newCapacity <= capacity_) { return; }
        assert(alloc_ != nullptr && "Queue: no allocator");
        T* newData = static_cast<T*>(alloc_->Alloc(newCapacity * sizeof(T), tag_));
        assert(newData != nullptr && "Queue: allocation failed");
        for (size_t i = 0; i < size_; ++i) {
            size_t src = (head_ + i) % capacity_;
            new(newData + i) T(std::move(data_[src]));
            data_[src].~T();
        }
        if (data_) {
            alloc_->Free(data_);
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

    void Reset()
    {
        Clear();
        if (data_) {
            alloc_->Free(data_);
            data_ = nullptr;
            capacity_ = 0;
        }
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

    TlsfAllocator* alloc_{};
    AllocTag tag_{AllocTag::Unknown};
    T* data_{};
    size_t head_{0};
    size_t size_{0};
    size_t capacity_{0};
};
} // namespace Core

#endif // WILL_ENGINE_QUEUE_H
