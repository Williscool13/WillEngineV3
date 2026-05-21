//
// Created by William on 2026-05-04.
//

#ifndef WILL_ENGINE_INLINE_SET_H
#define WILL_ENGINE_INLINE_SET_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "hash.h"

namespace Core
{
/**
 * Open-addressing hash set with compile-time capacity. Storage is inline (embedded in the object),
 * so no allocator is needed. N must be a power of two.
 *
 * PREFER this over FixedSet or Set when the maximum entry count is a known compile-time constant.
 * Use FixedSet when capacity is only known at runtime.
 * Use Set as a last resort for truly unbounded growth.
 */
template<typename K, size_t N, typename H = Hash<K>>
class InlineSet
{
    static_assert(N > 0 && (N & (N - 1)) == 0, "InlineSet: N must be a non-zero power of two");

    struct Slot
    {
        enum class State : uint8_t { Empty, Occupied, Dead };

        alignas(K) unsigned char keyBuf[sizeof(K)];
        State state{State::Empty};

        K& Key() { return *reinterpret_cast<K*>(keyBuf); }
        const K& Key() const { return *reinterpret_cast<const K*>(keyBuf); }
    };

public:
    InlineSet() = default;

    ~InlineSet() { Clear(); }

    InlineSet(const InlineSet& other)
    {
        for (size_t i = 0; i < N; ++i) {
            if (other.slots_[i].state == Slot::State::Occupied) {
                Insert(other.slots_[i].Key());
            }
        }
    }

    InlineSet& operator=(const InlineSet& other)
    {
        if (this == &other) { return *this; }
        Clear();
        for (size_t i = 0; i < N; ++i) {
            if (other.slots_[i].state == Slot::State::Occupied) {
                Insert(other.slots_[i].Key());
            }
        }
        return *this;
    }

    InlineSet(InlineSet&& other) noexcept
    {
        for (size_t i = 0; i < N; ++i) {
            if (other.slots_[i].state == Slot::State::Occupied) {
                new(slots_[i].keyBuf) K(std::move(other.slots_[i].Key()));
                slots_[i].state = Slot::State::Occupied;
                other.slots_[i].Key().~K();
                other.slots_[i].state = Slot::State::Empty;
            }
        }
        size_ = other.size_;
        other.size_ = 0;
    }

    InlineSet& operator=(InlineSet&& other) noexcept
    {
        if (this == &other) { return *this; }
        Clear();
        for (size_t i = 0; i < N; ++i) {
            if (other.slots_[i].state == Slot::State::Occupied) {
                new(slots_[i].keyBuf) K(std::move(other.slots_[i].Key()));
                slots_[i].state = Slot::State::Occupied;
                other.slots_[i].Key().~K();
                other.slots_[i].state = Slot::State::Empty;
            }
        }
        size_ = other.size_;
        other.size_ = 0;
        return *this;
    }

    /** Inserts key; no-op if already present. Returns true if inserted. */
    bool Insert(const K& key)
    {
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < N; ++probe) {
            const size_t idx = (h + probe) & (N - 1);
            Slot& s = slots_[idx];
            if (s.state == Slot::State::Occupied && s.Key() == key) { return false; }
            if (s.state == Slot::State::Empty || s.state == Slot::State::Dead) {
                new(s.keyBuf) K(key);
                s.state = Slot::State::Occupied;
                ++size_;
                return true;
            }
        }
        assert(false && "InlineSet: table full");
        return false;
    }

    bool Contains(const K& key) const
    {
        const uint64_t h = H{}(key);
        for (size_t probe = 0; probe < N; ++probe) {
            const size_t idx = (h + probe) & (N - 1);
            const Slot& s = slots_[idx];
            if (s.state == Slot::State::Empty) { return false; }
            if (s.state == Slot::State::Occupied && s.Key() == key) { return true; }
        }
        return false;
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
                s.state = Slot::State::Dead;
                --size_;
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

        const K& operator*() { return slots[index].Key(); }
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

        const K& operator*() const { return slots[index].Key(); }
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
    Slot slots_[N]{};
    size_t size_{};
};
} // namespace Core

#endif // WILL_ENGINE_INLINE_SET_H
