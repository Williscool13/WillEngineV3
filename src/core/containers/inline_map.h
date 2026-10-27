//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_INLINE_MAP_H
#define WILL_ENGINE_INLINE_MAP_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "hash.h"

namespace Core
{
/**
 * Open-addressing hash map with compile-time capacity. Storage is inline (embedded in the object),
 * so no allocator is needed. N must be a power of two.
 *
 * PREFER this over FixedMap or Map when the maximum entry count is a known compile-time constant.
 * Use FixedMap when capacity is only known at runtime.
 * Use Map as a last resort for truly unbounded growth.
 */
template<typename K, typename V, size_t N, typename H = Hash<K> >
class InlineMap
{
    static_assert(N > 0 && (N & (N - 1)) == 0, "InlineMap: N must be a non-zero power of two");

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
    InlineMap() = default;

    ~InlineMap() { Clear(); }

    InlineMap(const InlineMap& other)
    {
        for (size_t i = 0; i < N; ++i) {
            if (other.slots_[i].state == Slot::State::Occupied) {
                Insert(other.slots_[i].Key(), other.slots_[i].Val());
            }
        }
    }

    InlineMap& operator=(const InlineMap& other)
    {
        if (this == &other) { return *this; }
        Clear();
        for (size_t i = 0; i < N; ++i) {
            if (other.slots_[i].state == Slot::State::Occupied) {
                Insert(other.slots_[i].Key(), other.slots_[i].Val());
            }
        }
        return *this;
    }

    InlineMap(InlineMap&& other) noexcept
    {
        for (size_t i = 0; i < N; ++i) {
            if (other.slots_[i].state == Slot::State::Occupied) {
                new(slots_[i].keyBuf) K(std::move(other.slots_[i].Key()));
                new(slots_[i].valBuf) V(std::move(other.slots_[i].Val()));
                slots_[i].state = Slot::State::Occupied;
                other.slots_[i].Key().~K();
                other.slots_[i].Val().~V();
                other.slots_[i].state = Slot::State::Empty;
            }
        }
        size_ = other.size_;
        other.size_ = 0;
    }

    InlineMap& operator=(InlineMap&& other) noexcept
    {
        if (this == &other) { return *this; }
        Clear();
        for (size_t i = 0; i < N; ++i) {
            if (other.slots_[i].state == Slot::State::Occupied) {
                new(slots_[i].keyBuf) K(std::move(other.slots_[i].Key()));
                new(slots_[i].valBuf) V(std::move(other.slots_[i].Val()));
                slots_[i].state = Slot::State::Occupied;
                other.slots_[i].Key().~K();
                other.slots_[i].Val().~V();
                other.slots_[i].state = Slot::State::Empty;
            }
        }
        size_ = other.size_;
        other.size_ = 0;
        return *this;
    }

    V& operator[](const K& key)
    {
        bool bFound = false;
        Slot& s = Locate(key, bFound);
        if (bFound) { return s.Val(); }
        new(s.keyBuf) K(key);
        new(s.valBuf) V();
        Occupy(s);
        return s.Val();
    }

    void Insert(const K& key, const V& value)
    {
        bool bFound = false;
        Slot& s = Locate(key, bFound);
        if (bFound) {
            s.Val() = value;
            return;
        }
        new(s.keyBuf) K(key);
        new(s.valBuf) V(value);
        Occupy(s);
    }

    void Insert(const K& key, V&& value)
    {
        bool bFound = false;
        Slot& s = Locate(key, bFound);
        if (bFound) {
            s.Val() = std::move(value);
            return;
        }
        new(s.keyBuf) K(key);
        new(s.valBuf) V(std::move(value));
        Occupy(s);
    }

    template<typename... Args>
    V& Emplace(const K& key, Args&&... args)
    {
        bool bFound = false;
        Slot& s = Locate(key, bFound);
        if (bFound) { return s.Val(); }
        new(s.keyBuf) K(key);
        new(s.valBuf) V(std::forward<Args>(args)...);
        Occupy(s);
        return s.Val();
    }

    struct EmplaceResult { V& value; bool inserted; };

    template<typename... Args>
    EmplaceResult TryEmplace(const K& key, Args&&... args)
    {
        bool bFound = false;
        Slot& s = Locate(key, bFound);
        if (bFound) { return {s.Val(), false}; }
        new(s.keyBuf) K(key);
        new(s.valBuf) V(std::forward<Args>(args)...);
        Occupy(s);
        return {s.Val(), true};
    }

    V* Find(const K& key)
    {
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < N; ++probe) {
            const size_t idx = (h + probe) & (N - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Empty) { return nullptr; }
            if (s.state == Slot::State::Occupied && s.Key() == key) { return &s.Val(); }
        }
        return nullptr;
    }

    const V* Find(const K& key) const
    {
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < N; ++probe) {
            const size_t idx = (h + probe) & (N - 1);
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
        assert(v != nullptr && "InlineMap::At: key not found");
        return *v;
    }

    bool Remove(const K& key)
    {
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < N; ++probe) {
            const size_t idx = (h + probe) & (N - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Empty) { return false; }
            if (s.state == Slot::State::Occupied && s.Key() == key) {
                s.Key().~K();
                s.Val().~V();
                s.state = Slot::State::Dead;
                --size_;
                ClearTrailingTombstones(idx);
                return true;
            }
        }
        return false;
    }

    void Clear()
    {
        for (size_t i = 0; i < N; ++i) {
            if (slots_[i].state == Slot::State::Occupied) {
                slots_[i].Key().~K();
                slots_[i].Val().~V();
            }
            slots_[i].state = Slot::State::Empty;
        }
        size_ = 0;
    }

    size_t Size() const { return size_; }
    constexpr size_t GetCapacity() const { return N; }
    bool IsEmpty() const { return size_ == 0; }

    struct Iterator
    {
        Slot* slots;
        size_t index;

        void Advance()
        {
            ++index;
            while (index < N && slots[index].state != Slot::State::Occupied) { ++index; }
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
        size_t index;

        void Advance()
        {
            ++index;
            while (index < N && slots[index].state != Slot::State::Occupied) { ++index; }
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
        while (i < N && slots_[i].state != Slot::State::Occupied) { ++i; }
        return {slots_, i};
    }

    Iterator end() { return {slots_, N}; }

    ConstIterator begin() const
    {
        size_t i = 0;
        while (i < N && slots_[i].state != Slot::State::Occupied) { ++i; }
        return {slots_, i};
    }

    ConstIterator end() const { return {slots_, N}; }

    ConstIterator cbegin() const { return begin(); }
    ConstIterator cend() const { return end(); }

private:
    Slot& Locate(const K& key, bool& bFound)
    {
        const uint64_t h = H{}(key);
        Slot* firstDead = nullptr;
        for (size_t probe = 0; probe < N; ++probe) {
            Slot& s = slots_[(h + probe) & (N - 1)];
            if (s.state == Slot::State::Occupied) {
                if (s.Key() == key) {
                    bFound = true;
                    return s;
                }
                continue;
            }
            if (s.state == Slot::State::Dead) {
                if (firstDead == nullptr) { firstDead = &s; }
                continue;
            }
            bFound = false;
            return firstDead != nullptr ? *firstDead : s;
        }
        assert(firstDead != nullptr && "InlineMap: table full");
        bFound = false;
        return firstDead != nullptr ? *firstDead : slots_[0];
    }

    void Occupy(Slot& s)
    {
        s.state = Slot::State::Occupied;
        ++size_;
    }

    void ClearTrailingTombstones(size_t idx)
    {
        while (slots_[idx].state == Slot::State::Dead && slots_[(idx + 1) & (N - 1)].state == Slot::State::Empty) {
            slots_[idx].state = Slot::State::Empty;
            idx = (idx - 1) & (N - 1);
        }
    }

    Slot slots_[N]{};
    size_t size_{};
};
} // namespace Core

#endif // WILL_ENGINE_INLINE_MAP_H
