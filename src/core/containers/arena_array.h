//
// Created by William on 2026-04-06.
//

#ifndef WILL_ENGINE_ARENA_ARRAY_H
#define WILL_ENGINE_ARENA_ARRAY_H

#include <cassert>
#include <cstddef>
#include <new>
#include <utility>

#include "core/memory/arena.h"

namespace Core
{
/**
 * Fixed-size array with runtime capacity backed by an Arena.
 * All N slots are always live (constructed at init, destroyed at clear).
 * Size equals capacity; there is no push/pop interface.
 * Does NOT free memory; the arena is bulk-reset externally.
 *
 * PREFER this over ArenaFixedVector when all slots are always valid.
 * Use ArenaFixedVector when only a subset of slots are occupied at a time.
 * Use only with arenas that outlive the container (e.g. per-frame render arena).
 */
template<typename T>
class ArenaArray
{
public:
    ArenaArray() = default;

    ArenaArray(Arena* arena, size_t size)
        : arena_(arena), size_(size)
    {
        assert(arena_ != nullptr && "ArenaArray: no arena");
        assert(size_ > 0 && "ArenaArray: size must be > 0");
        data_ = static_cast<T*>(arena_->AllocRaw(size_ * sizeof(T), alignof(T)));
        assert(data_ != nullptr && "ArenaArray: allocation failed");
        for (size_t i = 0; i < size_; ++i) {
            new(data_ + i) T();
        }
    }

    ~ArenaArray() { Clear(); }

    ArenaArray(const ArenaArray& other)
        : arena_(other.arena_), size_(other.size_)
    {
        if (other.data_) {
            data_ = static_cast<T*>(arena_->AllocRaw(size_ * sizeof(T), alignof(T)));
            assert(data_ != nullptr && "ArenaArray: allocation failed");
            for (size_t i = 0; i < size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
        }
    }

    ArenaArray& operator=(const ArenaArray& other)
    {
        if (this == &other) { return *this; }
        Clear();
        arena_ = other.arena_;
        size_  = other.size_;
        if (other.data_) {
            data_ = static_cast<T*>(arena_->AllocRaw(size_ * sizeof(T), alignof(T)));
            assert(data_ != nullptr && "ArenaArray: allocation failed");
            for (size_t i = 0; i < size_; ++i) {
                new(data_ + i) T(other.data_[i]);
            }
        }
        return *this;
    }

    ArenaArray(ArenaArray&& other) noexcept
        : arena_(other.arena_),
          data_(other.data_), size_(other.size_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    ArenaArray& operator=(ArenaArray&& other) noexcept
    {
        if (this == &other) { return *this; }
        Clear();
        arena_      = other.arena_;
        data_       = other.data_;
        size_       = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
        return *this;
    }

    // Calls destructors on all elements. Memory remains owned by the arena.
    void Clear()
    {
        if (data_) {
            for (size_t i = 0; i < size_; ++i) { data_[i].~T(); }
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
    Arena*  arena_{};
    T*      data_{};
    size_t  size_{};
};
} // namespace Core

#endif // WILL_ENGINE_ARENA_ARRAY_H
