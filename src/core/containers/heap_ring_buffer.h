//
// Created by William on 2026-04-08.
//

#ifndef WILL_ENGINE_HEAP_RING_BUFFER_H
#define WILL_ENGINE_HEAP_RING_BUFFER_H

#include <cassert>
#include <cstddef>
#include <new>
#include <utility>

#include "core/memory/tlsf_allocator.h"

namespace Core
{
/**
 * Fixed-capacity ring buffer backed by a TlsfAllocator.
 * All capacity slots are always live (constructed at init, destroyed at reset).
 * Push overwrites the oldest element when full. Pop moves out the front element.
 * Capacity must be a power of 2.
 *
 * PREFER this over RingBuffer when runtime capacity is needed.
 * Use RingBuffer for compile-time fixed capacity.
 */
template<typename T>
class HeapRingBuffer
{
public:
    HeapRingBuffer() = default;

    HeapRingBuffer(TlsfAllocator* alloc, AllocTag tag, size_t capacity)
        : alloc_(alloc), tag_(tag), capacity_(capacity), mask_(capacity - 1)
    {
        assert(alloc_ != nullptr && "HeapRingBuffer: no allocator");
        assert(capacity_ > 0 && "HeapRingBuffer: capacity must be > 0");
        assert((capacity_ & mask_) == 0 && "HeapRingBuffer: capacity must be a power of 2");
        data_ = static_cast<T*>(alloc_->Alloc(capacity_ * sizeof(T), tag_));
        assert(data_ != nullptr && "OOM: HeapRingBuffer allocation failed");
        for (size_t i = 0; i < capacity_; ++i) {
            new(data_ + i) T();
        }
    }

    ~HeapRingBuffer() { Reset(); }

    HeapRingBuffer(const HeapRingBuffer& other)
        : alloc_(other.alloc_), tag_(other.tag_),
          capacity_(other.capacity_), mask_(other.mask_),
          head_(other.head_), tail_(other.tail_)
    {
        if (other.data_) {
            data_ = static_cast<T*>(alloc_->Alloc(capacity_ * sizeof(T), tag_));
            assert(data_ != nullptr && "HeapRingBuffer: allocation failed");
            for (size_t i = 0; i < capacity_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
        }
    }

    HeapRingBuffer& operator=(const HeapRingBuffer& other)
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_    = other.alloc_;
        tag_      = other.tag_;
        capacity_ = other.capacity_;
        mask_     = other.mask_;
        head_     = other.head_;
        tail_     = other.tail_;
        if (other.data_) {
            data_ = static_cast<T*>(alloc_->Alloc(capacity_ * sizeof(T), tag_));
            assert(data_ != nullptr && "HeapRingBuffer: allocation failed");
            for (size_t i = 0; i < capacity_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
        }
        return *this;
    }

    HeapRingBuffer(HeapRingBuffer&& other) noexcept
        : alloc_(other.alloc_), tag_(other.tag_),
          data_(other.data_), capacity_(other.capacity_), mask_(other.mask_),
          head_(other.head_), tail_(other.tail_)
    {
        other.data_     = nullptr;
        other.capacity_ = 0;
        other.mask_     = 0;
        other.head_     = 0;
        other.tail_     = 0;
    }

    HeapRingBuffer& operator=(HeapRingBuffer&& other) noexcept
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_          = other.alloc_;
        tag_            = other.tag_;
        data_           = other.data_;
        capacity_       = other.capacity_;
        mask_           = other.mask_;
        head_           = other.head_;
        tail_           = other.tail_;
        other.data_     = nullptr;
        other.capacity_ = 0;
        other.mask_     = 0;
        other.head_     = 0;
        other.tail_     = 0;
        return *this;
    }

    void Reset()
    {
        if (data_) {
            for (size_t i = 0; i < capacity_; ++i) { data_[i].~T(); }
            alloc_->Free(data_);
            data_     = nullptr;
            capacity_ = 0;
            mask_     = 0;
            head_     = 0;
            tail_     = 0;
        }
    }

    void Push(const T& item)
    {
        if (IsFull()) { head_++; }
        data_[tail_ & mask_] = item;
        tail_++;
    }

    void Push(T&& item)
    {
        if (IsFull()) { head_++; }
        data_[tail_ & mask_] = std::move(item);
        tail_++;
    }

    bool Pop(T& out)
    {
        if (IsEmpty()) { return false; }
        out = std::move(data_[head_ & mask_]);
        head_++;
        return true;
    }

    void Clear()
    {
        head_ = 0;
        tail_ = 0;
    }

    T&       Front()       { assert(!IsEmpty()); return data_[head_ & mask_]; }
    const T& Front() const { assert(!IsEmpty()); return data_[head_ & mask_]; }
    T&       Back()        { assert(!IsEmpty()); return data_[(tail_ - 1) & mask_]; }
    const T& Back()  const { assert(!IsEmpty()); return data_[(tail_ - 1) & mask_]; }

    T&       GetAt(size_t i)       { assert(i < Size()); return data_[(head_ + i) & mask_]; }
    const T& GetAt(size_t i) const { assert(i < Size()); return data_[(head_ + i) & mask_]; }

    template<typename Func>
    void ForEach(Func&& fn) const
    {
        for (size_t i = head_; i != tail_; ++i) {
            fn(data_[i & mask_]);
        }
    }

    size_t Size()        const { return tail_ - head_; }
    size_t Capacity()    const { return capacity_; }
    bool   IsEmpty()     const { return head_ == tail_; }
    bool   IsFull()      const { return (tail_ - head_) >= capacity_; }
    bool   IsAllocated() const { return data_ != nullptr; }

private:
    TlsfAllocator* alloc_{};
    AllocTag       tag_{AllocTag::Unknown};
    T*             data_{};
    size_t         capacity_{};
    size_t         mask_{};
    size_t         head_{};
    size_t         tail_{};
};
} // namespace Core

#endif // WILL_ENGINE_HEAP_RING_BUFFER_H
