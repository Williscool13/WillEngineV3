//
// Created by William on 2026-05-04.
//

#ifndef WILL_ENGINE_SET_H
#define WILL_ENGINE_SET_H

#include <cassert>
#include <cstddef>
#include <new>
#include <utility>

#include "hash.h"
#include "core/memory/tlsf_allocator.h"

namespace Core
{
/**
 * Open-addressing hash set backed by a TlsfAllocator. Rehashes on growth (0.75 load factor).
 * Asserts if the table is full (should never happen given the load factor).
 *
 * PREFER FixedSet when the maximum entry count is known at init time.
 * Use InlineSet when capacity is a compile-time constant.
 * Use Set as a last resort for truly unbounded growth.
 *
 * Provide a Hash<K> specialization in hash.h for custom key types.
 */
template<typename K, typename H = Hash<K>>
class Set
{
    struct Slot
    {
        enum class State : uint8_t { Empty, Occupied, Dead };

        alignas(K) unsigned char keyBuf[sizeof(K)];
        State state{State::Empty};

        K& Key() { return *reinterpret_cast<K*>(keyBuf); }
        const K& Key() const { return *reinterpret_cast<const K*>(keyBuf); }
    };

public:
    Set() = default;

    Set(TlsfAllocator* alloc, AllocTag tag, size_t initialCapacity = 0)
        : alloc_(alloc), tag_(tag)
    {
        if (initialCapacity > 0) {
            Rehash(NextPow2(initialCapacity));
        }
    }

    ~Set() { Reset(); }

    Set(const Set& other)
        : alloc_(other.alloc_), tag_(other.tag_)
    {
        if (other.size_ > 0) {
            Rehash(other.capacity_);
            for (size_t i = 0; i < other.capacity_; ++i) {
                if (other.slots_[i].state == Slot::State::Occupied) {
                    Insert(other.slots_[i].Key());
                }
            }
        }
    }

    Set& operator=(const Set& other)
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_ = other.alloc_;
        tag_ = other.tag_;
        if (other.size_ > 0) {
            Rehash(other.capacity_);
            for (size_t i = 0; i < other.capacity_; ++i) {
                if (other.slots_[i].state == Slot::State::Occupied) {
                    Insert(other.slots_[i].Key());
                }
            }
        }
        return *this;
    }

    Set(Set&& other) noexcept
        : alloc_(other.alloc_), tag_(other.tag_),
          slots_(other.slots_), size_(other.size_), capacity_(other.capacity_)
    {
        other.slots_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    Set& operator=(Set&& other) noexcept
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_ = other.alloc_;
        tag_ = other.tag_;
        slots_ = other.slots_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.slots_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        return *this;
    }

    /** Inserts key; no-op if already present. Returns true if inserted. */
    bool Insert(const K& key)
    {
        EnsureCapacity();
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) { return false; }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(key);
                s.state = Slot::State::Occupied;
                ++size_;
                return true;
            }
        }
        assert(false && "Set: table full");
        return false;
    }

    bool Contains(const K& key) const
    {
        if (capacity_ == 0) { return false; }
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            const Slot& s = slots_[idx];
            if (s.state == Slot::State::Empty) { return false; }
            if (s.state == Slot::State::Occupied && s.Key() == key) { return true; }
        }
        return false;
    }

    bool Remove(const K& key)
    {
        if (capacity_ == 0) { return false; }
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Empty) { return false; }
            if (s.state == Slot::State::Occupied && s.Key() == key) {
                s.Key().~K();
                s.state = Slot::State::Dead;
                --size_;
                return true;
            }
        }
        return false;
    }

    void Clear()
    {
        for (size_t i = 0; i < capacity_; ++i) {
            if (slots_[i].state == Slot::State::Occupied) {
                slots_[i].Key().~K();
            }
            slots_[i].state = Slot::State::Empty;
        }
        size_ = 0;
    }

    void Reset()
    {
        if (slots_) {
            for (size_t i = 0; i < capacity_; ++i) {
                if (slots_[i].state == Slot::State::Occupied) {
                    slots_[i].Key().~K();
                }
            }
            alloc_->Free(slots_);
            slots_ = nullptr;
            size_ = 0;
            capacity_ = 0;
        }
    }

    size_t Size() const { return size_; }
    size_t GetCapacity() const { return capacity_; }
    bool   IsEmpty() const { return size_ == 0; }
    bool   IsAllocated() const { return slots_ != nullptr; }

    struct Iterator
    {
        Slot* slots;
        size_t capacity;
        size_t index;

        void Advance()
        {
            ++index;
            while (index < capacity && slots[index].state != Slot::State::Occupied) { ++index; }
        }

        bool operator!=(const Iterator& other) const { return index != other.index; }

        Iterator& operator++()
        {
            Advance();
            return *this;
        }

        const K& operator*() { return slots[index].Key(); }
    };

    struct ConstIterator
    {
        const Slot* slots;
        size_t capacity;
        size_t index;

        void Advance()
        {
            ++index;
            while (index < capacity && slots[index].state != Slot::State::Occupied) { ++index; }
        }

        bool operator!=(const ConstIterator& other) const { return index != other.index; }

        ConstIterator& operator++()
        {
            Advance();
            return *this;
        }

        const K& operator*() const { return slots[index].Key(); }
    };

    Iterator begin()
    {
        size_t i = 0;
        while (i < capacity_ && slots_[i].state != Slot::State::Occupied) { ++i; }
        return {slots_, capacity_, i};
    }

    Iterator end() { return {slots_, capacity_, capacity_}; }

    ConstIterator begin() const
    {
        size_t i = 0;
        while (i < capacity_ && slots_[i].state != Slot::State::Occupied) { ++i; }
        return {slots_, capacity_, i};
    }

    ConstIterator end() const { return {slots_, capacity_, capacity_}; }

    ConstIterator cbegin() const { return begin(); }
    ConstIterator cend() const { return end(); }

private:
    void EnsureCapacity()
    {
        if (capacity_ == 0 || (size_ + 1) * 4 > capacity_ * 3) {
            Rehash(capacity_ < 8 ? 8 : capacity_ * 2);
        }
    }

    void Rehash(size_t newCapacity)
    {
        assert(alloc_ != nullptr && "Set: no allocator");
        Slot* oldSlots = slots_;
        size_t oldCapacity = capacity_;

        slots_ = static_cast<Slot*>(alloc_->Alloc(newCapacity * sizeof(Slot), tag_));
        assert(slots_ != nullptr && "Set: allocation failed");
        capacity_ = newCapacity;
        size_ = 0;

        for (size_t i = 0; i < newCapacity; ++i) {
            slots_[i].state = Slot::State::Empty;
        }

        for (size_t i = 0; i < oldCapacity; ++i) {
            if (oldSlots[i].state == Slot::State::Occupied) {
                Insert(std::move(oldSlots[i].Key()));
                oldSlots[i].Key().~K();
            }
        }

        if (oldSlots) {
            alloc_->Free(oldSlots);
        }
    }

    bool Insert(K&& key)
    {
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) { return false; }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(std::move(key));
                s.state = Slot::State::Occupied;
                ++size_;
                return true;
            }
        }
        assert(false && "Set: table full");
        return false;
    }

    static size_t NextPow2(size_t n)
    {
        if (n == 0) { return 1; }
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    TlsfAllocator* alloc_{};
    AllocTag tag_{AllocTag::Unknown};
    Slot* slots_{};
    size_t size_{};
    size_t capacity_{};
};
} // namespace Core

#endif // WILL_ENGINE_SET_H
