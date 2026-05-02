//
// Created by William on 2026-04-01.
//

#ifndef WILL_ENGINE_ARENA_FIXED_MAP_H
#define WILL_ENGINE_ARENA_FIXED_MAP_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "hash.h"
#include "core/memory/arena.h"

namespace Core
{
/**
 * Open-addressing hash map with runtime-fixed capacity backed by an Arena.
 * Capacity is rounded up to the next power of two at construction and never changes.
 * Asserts on overflow. No rehashing.
 * Clear() calls element destructors. Does NOT free memory; the arena is bulk-reset externally.
 *
 * PREFER this over ArenaFixedVector when key-based lookup is needed for scratch data.
 * Use only with arenas that outlive the container (e.g. per-frame render arena).
 */
template<typename K, typename V, typename H = Hash<K> >
class ArenaFixedMap
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
    ArenaFixedMap() = default;

    ArenaFixedMap(Arena* arena, size_t capacity)
        : arena_(arena)
    {
        assert(arena_ != nullptr && "ArenaFixedMap: no arena");
        assert(capacity > 0 && "ArenaFixedMap: capacity must be > 0");
        capacity_ = NextPow2(capacity);
        slots_ = static_cast<Slot*>(arena_->AllocRaw(capacity_ * sizeof(Slot), alignof(Slot)));
        assert(slots_ != nullptr && "ArenaFixedMap: allocation failed");
        for (size_t i = 0; i < capacity_; ++i) {
            slots_[i].state = Slot::State::Empty;
        }
    }

    ~ArenaFixedMap() { Clear(); }

    ArenaFixedMap(const ArenaFixedMap& other)
        : arena_(other.arena_), capacity_(other.capacity_)
    {
        if (other.slots_) {
            slots_ = static_cast<Slot*>(arena_->AllocRaw(capacity_ * sizeof(Slot), alignof(Slot)));
            assert(slots_ != nullptr && "ArenaFixedMap: allocation failed");
            for (size_t i = 0; i < capacity_; ++i) { slots_[i].state = Slot::State::Empty; }
            for (size_t i = 0; i < capacity_; ++i) {
                if (other.slots_[i].state == Slot::State::Occupied) {
                    Insert(other.slots_[i].Key(), other.slots_[i].Val());
                }
            }
        }
    }

    ArenaFixedMap& operator=(const ArenaFixedMap& other)
    {
        if (this == &other) { return *this; }
        Clear();
        arena_ = other.arena_;
        capacity_ = other.capacity_;
        if (other.slots_) {
            slots_ = static_cast<Slot*>(arena_->AllocRaw(capacity_ * sizeof(Slot), alignof(Slot)));
            assert(slots_ != nullptr && "ArenaFixedMap: allocation failed");
            for (size_t i = 0; i < capacity_; ++i) { slots_[i].state = Slot::State::Empty; }
            for (size_t i = 0; i < capacity_; ++i) {
                if (other.slots_[i].state == Slot::State::Occupied) {
                    Insert(other.slots_[i].Key(), other.slots_[i].Val());
                }
            }
        }
        return *this;
    }

    ArenaFixedMap(ArenaFixedMap&& other) noexcept
        : arena_(other.arena_),
          slots_(other.slots_), size_(other.size_), capacity_(other.capacity_)
    {
        other.slots_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    ArenaFixedMap& operator=(ArenaFixedMap&& other) noexcept
    {
        if (this == &other) { return *this; }
        Clear();
        arena_ = other.arena_;
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
        assert(slots_ != nullptr && "ArenaFixedMap: not initialized");
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
        assert(false && "ArenaFixedMap: table full");
        return slots_[0].Val();
    }

    void Insert(const K& key, const V& value)
    {
        assert(slots_ != nullptr && "ArenaFixedMap: not initialized");
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
        assert(false && "ArenaFixedMap: table full");
    }

    void Insert(const K& key, V&& value)
    {
        assert(slots_ != nullptr && "ArenaFixedMap: not initialized");
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
        assert(false && "ArenaFixedMap: table full");
    }

    template<typename... Args>
    V& Emplace(const K& key, Args&&... args)
    {
        assert(slots_ != nullptr && "ArenaFixedMap: not initialized");
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
        assert(false && "ArenaFixedMap: table full");
        return slots_[0].Val();
    }

    struct EmplaceResult { V& value; bool inserted; };

    template<typename... Args>
    EmplaceResult TryEmplace(const K& key, Args&&... args)
    {
        assert(slots_ != nullptr && "ArenaFixedMap: not initialized");
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) {
                return {s.Val(), false};
            }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(key);
                new(s.valBuf) V(std::forward<Args>(args)...);
                s.state = Slot::State::Occupied;
                ++size_;
                return {s.Val(), true};
            }
        }
        assert(false && "ArenaFixedMap: table full");
        return {slots_[0].Val(), false};
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
        assert(v != nullptr && "ArenaFixedMap::At: key not found");
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

    Arena* arena_{};
    Slot* slots_{};
    size_t size_{};
    size_t capacity_{};
};
} // namespace Core

#endif // WILL_ENGINE_ARENA_FIXED_MAP_H
