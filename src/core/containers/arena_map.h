//
// Created by William on 2026-05-19.
//

#ifndef WILL_ENGINE_ARENA_MAP_H
#define WILL_ENGINE_ARENA_MAP_H

#include <cassert>
#include <cstddef>
#include <new>
#include <utility>

#include "hash.h"
#include "core/memory/arena.h"

namespace Core
{
/**
 * Open-addressing hash map backed by an Arena. Rehashes at 0.75 load factor by allocating
 * a new block from the arena; the old block is abandoned until the arena is bulk-reset.
 * Asserts if the probe loop exhausts the table (should never happen given the load factor).
 * Clear() calls element destructors but does NOT free memory.
 * Reset() additionally forgets the allocation.
 *
 * Suitable for per-frame or per-scope scratch data where the arena outlives the map.
 * PREFER ArenaFixedMap when the maximum entry count is known at construction time.
 */
template<typename K, typename V, typename H = Hash<K>>
class ArenaMap
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
    ArenaMap() = default;

    ArenaMap(Arena* arena, size_t initialCapacity = 0)
        : arena_(arena)
    {
        if (initialCapacity > 0) {
            Rehash(NextPow2(initialCapacity));
        }
    }

    ~ArenaMap() { Clear(); }

    ArenaMap(const ArenaMap& other)
        : arena_(other.arena_)
    {
        if (other.size_ > 0) {
            Rehash(other.capacity_);
            for (size_t i = 0; i < other.capacity_; ++i) {
                const Slot& s = other.slots_[i];
                if (s.state == Slot::State::Occupied) {
                    Insert(s.Key(), s.Val());
                }
            }
        }
    }

    ArenaMap& operator=(const ArenaMap& other)
    {
        if (this == &other) { return *this; }
        Clear();
        arena_ = other.arena_;
        if (other.size_ > 0) {
            Rehash(other.capacity_);
            for (size_t i = 0; i < other.capacity_; ++i) {
                const Slot& s = other.slots_[i];
                if (s.state == Slot::State::Occupied) {
                    Insert(s.Key(), s.Val());
                }
            }
        }
        return *this;
    }

    ArenaMap(ArenaMap&& other) noexcept
        : arena_(other.arena_),
          slots_(other.slots_), size_(other.size_), capacity_(other.capacity_)
    {
        other.slots_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    ArenaMap& operator=(ArenaMap&& other) noexcept
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
        EnsureCapacity();
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) {
                return s.Val();
            }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(key);
                new(s.valBuf) V();
                s.state = Slot::State::Occupied;
                ++size_;
                return s.Val();
            }
        }
        assert(false && "ArenaMap: table full");
        return slots_[0].Val();
    }

    void Insert(const K& key, const V& value)
    {
        EnsureCapacity();
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
        assert(false && "ArenaMap: table full");
    }

    void Insert(const K& key, V&& value)
    {
        EnsureCapacity();
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
        assert(false && "ArenaMap: table full");
    }

    template<typename... Args>
    V& Emplace(const K& key, Args&&... args)
    {
        EnsureCapacity();
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < capacity_; ++probe) {
            const size_t idx = (h + probe) & (capacity_ - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) {
                return s.Val();
            }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(key);
                new(s.valBuf) V(std::forward<Args>(args)...);
                s.state = Slot::State::Occupied;
                ++size_;
                return s.Val();
            }
        }
        assert(false && "ArenaMap: table full");
        return slots_[0].Val();
    }

    struct EmplaceResult { V& value; bool inserted; };

    template<typename... Args>
    EmplaceResult TryEmplace(const K& key, Args&&... args)
    {
        EnsureCapacity();
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
        assert(false && "ArenaMap: table full");
        return {slots_[0].Val(), false};
    }

    V* Find(const K& key)
    {
        if (capacity_ == 0) { return nullptr; }
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
        if (capacity_ == 0) { return nullptr; }
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
        assert(v != nullptr && "ArenaMap::At: key not found");
        return *v;
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

    // Calls destructors and forgets the allocation. Arena memory is reclaimed on arena reset.
    void Reset()
    {
        Clear();
        slots_ = nullptr;
        capacity_ = 0;
    }

    size_t Size() const { return size_; }
    size_t GetCapacity() const { return capacity_; }
    bool IsEmpty() const { return size_ == 0; }
    bool IsAllocated() const { return slots_ != nullptr; }

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

        Iterator& operator++() { Advance(); return *this; }

        struct KVPair { const K& key; V& value; };

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

        ConstIterator& operator++() { Advance(); return *this; }

        struct KVPair { const K& key; const V& value; };

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
    void EnsureCapacity()
    {
        if (capacity_ == 0 || (size_ + 1) * 4 > capacity_ * 3) {
            Rehash(capacity_ < 8 ? 8 : capacity_ * 2);
        }
    }

    void Rehash(size_t newCapacity)
    {
        assert(arena_ != nullptr && "ArenaMap: no arena");
        Slot* oldSlots = slots_;
        size_t oldCapacity = capacity_;

        slots_ = static_cast<Slot*>(arena_->AllocRaw(newCapacity * sizeof(Slot), alignof(Slot)));
        assert(slots_ != nullptr && "ArenaMap: allocation failed");
        capacity_ = newCapacity;
        size_ = 0;

        for (size_t i = 0; i < newCapacity; ++i) {
            slots_[i].state = Slot::State::Empty;
        }

        for (size_t i = 0; i < oldCapacity; ++i) {
            if (oldSlots[i].state == Slot::State::Occupied) {
                Insert(oldSlots[i].Key(), std::move(oldSlots[i].Val()));
                oldSlots[i].Key().~K();
                oldSlots[i].Val().~V();
            }
        }
        // oldSlots abandoned; reclaimed when the arena is reset
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

    Arena* arena_{};
    Slot* slots_{};
    size_t size_{};
    size_t capacity_{};
};
} // namespace Core

#endif // WILL_ENGINE_ARENA_MAP_H
