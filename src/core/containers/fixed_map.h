//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_FIXED_MAP_H
#define WILL_ENGINE_FIXED_MAP_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>

#include "hash.h"
#include "core/memory/tlsf_allocator.h"

namespace Core
{
/**
 * Open-addressing hash map with runtime-fixed capacity backed by a TlsfAllocator.
 * Capacity is rounded up to the next power of two at construction and never changes.
 * Asserts on overflow. No rehashing.
 *
 * PREFER this over Map when the maximum entry count is known at init time.
 * Use InlineMap when capacity is a compile-time constant.
 * Use Map as a last resort for truly unbounded growth.
 */
template<typename K, typename V, typename H = Hash<K> >
class FixedMap
{
    struct Slot
    {
        enum class State : uint8_t { Empty, Occupied, Dead };

        alignas(K) unsigned char keyBuf[sizeof(K)];
        alignas(V) unsigned char valBuf[sizeof(V)];
        State state{State::Empty};

        K& Key() { return *reinterpret_cast<K*>(keyBuf); }
        const K& Key() const { return *reinterpret_cast<const K*>(keyBuf); }
        V& Val() { return *reinterpret_cast<V*>(valBuf); }
        const V& Val() const { return *reinterpret_cast<const V*>(valBuf); }
    };

public:
    FixedMap() = default;

    FixedMap(TlsfAllocator* alloc, AllocTag tag, size_t capacity)
        : alloc_(alloc), tag_(tag)
    {
        assert(alloc_ != nullptr && "FixedMap: no allocator");
        assert(capacity > 0 && "FixedMap: capacity must be > 0");
        capacity_ = NextPow2(capacity);
        slots_ = static_cast<Slot*>(alloc_->Alloc(capacity_ * sizeof(Slot), tag_));
        assert(slots_ != nullptr && "FixedMap: allocation failed");
        for (size_t i = 0; i < capacity_; ++i) {
            slots_[i].state = Slot::State::Empty;
        }
    }

    ~FixedMap() { Reset(); }

    FixedMap(const FixedMap& other)
        : alloc_(other.alloc_), tag_(other.tag_), capacity_(other.capacity_)
    {
        if (other.slots_) {
            slots_ = static_cast<Slot*>(alloc_->Alloc(capacity_ * sizeof(Slot), tag_));
            assert(slots_ != nullptr && "FixedMap: allocation failed");
            for (size_t i = 0; i < capacity_; ++i) { slots_[i].state = Slot::State::Empty; }
            for (size_t i = 0; i < capacity_; ++i) {
                if (other.slots_[i].state == Slot::State::Occupied) {
                    Insert(other.slots_[i].Key(), other.slots_[i].Val());
                }
            }
        }
    }

    FixedMap& operator=(const FixedMap& other)
    {
        if (this == &other) { return *this; }
        Reset();
        alloc_ = other.alloc_;
        tag_ = other.tag_;
        capacity_ = other.capacity_;
        if (other.slots_) {
            slots_ = static_cast<Slot*>(alloc_->Alloc(capacity_ * sizeof(Slot), tag_));
            assert(slots_ != nullptr && "FixedMap: allocation failed");
            for (size_t i = 0; i < capacity_; ++i) { slots_[i].state = Slot::State::Empty; }
            for (size_t i = 0; i < capacity_; ++i) {
                if (other.slots_[i].state == Slot::State::Occupied) {
                    Insert(other.slots_[i].Key(), other.slots_[i].Val());
                }
            }
        }
        return *this;
    }

    FixedMap(FixedMap&& other) noexcept
        : alloc_(other.alloc_), tag_(other.tag_),
          slots_(other.slots_), size_(other.size_), capacity_(other.capacity_)
    {
        other.slots_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    FixedMap& operator=(FixedMap&& other) noexcept
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

    V& operator[](const K& key)
    {
        assert(slots_ != nullptr && "FixedMap: not initialized");
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) { return s.Val(); }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(key);
                new(s.valBuf) V();
                s.state = Slot::State::Occupied;
                ++size_;
                return s.Val();
            }
        }
        assert(false && "FixedMap: table full");
        return slots_[0].Val();
    }

    void Insert(const K& key, const V& value)
    {
        assert(slots_ != nullptr && "FixedMap: not initialized");
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) {
                s.Val() = value;
                return;
            }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(key);
                new(s.valBuf) V(value);
                s.state = Slot::State::Occupied;
                ++size_;
                return;
            }
        }
        assert(false && "FixedMap: table full");
    }

    void Insert(const K& key, V&& value)
    {
        assert(slots_ != nullptr && "FixedMap: not initialized");
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) {
                s.Val() = std::move(value);
                return;
            }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(key);
                new(s.valBuf) V(std::move(value));
                s.state = Slot::State::Occupied;
                ++size_;
                return;
            }
        }
        assert(false && "FixedMap: table full");
    }

    template<typename... Args>
    V& Emplace(const K& key, Args&&... args)
    {
        assert(slots_ != nullptr && "FixedMap: not initialized");
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) { return s.Val(); }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(key);
                new(s.valBuf) V(std::forward<Args>(args)...);
                s.state = Slot::State::Occupied;
                ++size_;
                return s.Val();
            }
        }
        assert(false && "FixedMap: table full");
        return slots_[0].Val();
    }

    V* Find(const K& key)
    {
        if (!slots_) { return nullptr; }
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Empty) { return nullptr; }
            if (s.state == Slot::State::Occupied && s.Key() == key) { return &s.Val(); }
        }
        return nullptr;
    }

    const V* Find(const K& key) const
    {
        if (!slots_) { return nullptr; }
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            const Slot& s = slots_[idx];
            if (s.state == Slot::State::Empty) { return nullptr; }
            if (s.state == Slot::State::Occupied && s.Key() == key) { return &s.Val(); }
        }
        return nullptr;
    }

    bool Contains(const K& key) const { return Find(key) != nullptr; }

    const V& At(const K& key) const
    {
        const V* v = Find(key);
        assert(v != nullptr && "FixedMap::At: key not found");
        return *v;
    }

    bool Remove(const K& key)
    {
        if (!slots_) { return false; }
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Empty) { return false; }
            if (s.state == Slot::State::Occupied && s.Key() == key) {
                s.Key().~K();
                s.Val().~V();
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
                slots_[i].Val().~V();
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
                    slots_[i].Val().~V();
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

        struct KVPair
        {
            const K& key;
            V& value;
        };

        KVPair operator*() { return {slots[index].Key(), slots[index].Val()}; }
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

        struct KVPair
        {
            const K& key;
            const V& value;
        };

        KVPair operator*() const { return {slots[index].Key(), slots[index].Val()}; }
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

#endif // WILL_ENGINE_FIXED_MAP_H
