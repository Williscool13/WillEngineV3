//
// Created by William on 2026-10-29.
//

#ifndef WILL_ENGINE_CONCURRENT_ID_MAP_H
#define WILL_ENGINE_CONCURRENT_ID_MAP_H

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

#include "core/containers/array.h"

namespace Core
{
/**
 * Fixed-capacity open-addressed uint64 -> uint64 map. Key 0 and value 0 are reserved.
 * Find, TryClaim and Publish are safe from any thread; Remove and ForEach are not and must not overlap them.
 * Insert is two-step: TryClaim takes the key slot, Publish stores the value. Find spins on a claimed slot until it is published.
 */
template<size_t Capacity>
class ConcurrentIdMap
{
    static_assert((Capacity & (Capacity - 1)) == 0, "ConcurrentIdMap: capacity must be a power of two");

    struct Slot
    {
        std::atomic<uint64_t> key{0};
        std::atomic<uint64_t> value{0};
    };

    Array<Slot, Capacity> slots{};
    std::atomic<uint32_t> count{0};

    static constexpr uint32_t MASK = static_cast<uint32_t>(Capacity - 1);

    static uint32_t Home(uint64_t key)
    {
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdull;
        key ^= key >> 33;
        return static_cast<uint32_t>(key) & MASK;
    }

    static uint64_t WaitPublished(const Slot& slot)
    {
        uint64_t value = slot.value.load(std::memory_order_acquire);
        while (value == 0) {
            std::this_thread::yield();
            value = slot.value.load(std::memory_order_acquire);
        }
        return value;
    }

public:
    static constexpr uint32_t INVALID_SLOT = ~0u;

    struct Claim
    {
        uint32_t slot{INVALID_SLOT};
        bool bClaimed{false};
    };

    /** @return the published value, or 0 if absent */
    uint64_t Find(uint64_t key) const
    {
        assert(key != 0);
        uint32_t i = Home(key);
        for (uint32_t probe = 0; probe < Capacity; ++probe, i = (i + 1) & MASK) {
            const uint64_t stored = slots[i].key.load(std::memory_order_acquire);
            if (stored == key) { return WaitPublished(slots[i]); }
            if (stored == 0) { return 0; }
        }
        return 0;
    }

    /** bClaimed: the caller owns the slot and must Publish it. Otherwise the key already exists at slot, or slot is INVALID_SLOT when full. */
    Claim TryClaim(uint64_t key)
    {
        assert(key != 0);
        uint32_t i = Home(key);
        for (uint32_t probe = 0; probe < Capacity; ++probe, i = (i + 1) & MASK) {
            uint64_t stored = slots[i].key.load(std::memory_order_acquire);
            if (stored == 0) {
                if (slots[i].key.compare_exchange_strong(stored, key, std::memory_order_acq_rel, std::memory_order_acquire)) {
                    count.fetch_add(1, std::memory_order_relaxed);
                    return {i, true};
                }
            }
            if (stored == key) { return {i, false}; }
        }
        return {};
    }

    void Publish(uint32_t slot, uint64_t value)
    {
        assert(value != 0);
        slots[slot].value.store(value, std::memory_order_release);
    }

    /** Backward-shift delete. Not thread-safe. */
    bool Remove(uint64_t key)
    {
        uint32_t i = Home(key);
        uint32_t probe = 0;
        for (; probe < Capacity; ++probe, i = (i + 1) & MASK) {
            const uint64_t stored = slots[i].key.load(std::memory_order_relaxed);
            if (stored == key) { break; }
            if (stored == 0) { return false; }
        }
        if (probe == Capacity) { return false; }

        uint32_t hole = i;
        for (uint32_t j = (hole + 1) & MASK; ; j = (j + 1) & MASK) {
            const uint64_t movingKey = slots[j].key.load(std::memory_order_relaxed);
            if (movingKey == 0) { break; }
            const uint32_t home = Home(movingKey);
            const bool bStays = hole <= j ? (hole < home && home <= j) : (hole < home || home <= j);
            if (bStays) { continue; }
            slots[hole].key.store(movingKey, std::memory_order_relaxed);
            slots[hole].value.store(slots[j].value.load(std::memory_order_relaxed), std::memory_order_relaxed);
            hole = j;
        }
        slots[hole].key.store(0, std::memory_order_relaxed);
        slots[hole].value.store(0, std::memory_order_relaxed);
        count.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    /** f(key, value) over published entries. Not thread-safe. */
    template<typename F>
    void ForEach(F&& f) const
    {
        for (const Slot& slot : slots) {
            const uint64_t key = slot.key.load(std::memory_order_relaxed);
            const uint64_t value = slot.value.load(std::memory_order_relaxed);
            if (key != 0 && value != 0) { f(key, value); }
        }
    }

    uint32_t Size() const { return count.load(std::memory_order_relaxed); }
};
} // Core

#endif //WILL_ENGINE_CONCURRENT_ID_MAP_H
