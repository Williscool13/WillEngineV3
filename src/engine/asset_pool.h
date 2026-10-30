//
// Created by William on 2026-10-29.
//

#ifndef WILL_ENGINE_ASSET_POOL_H
#define WILL_ENGINE_ASSET_POOL_H

#include <atomic>
#include <cassert>
#include <cstdint>

#include "core/containers/array.h"
#include "core/containers/concurrent_id_map.h"
#include "core/containers/concurrent_slot_pool.h"

namespace Engine
{
/**
 * Ref-counted, id-deduplicated asset slots.
 * AddRef, Acquire, Release, Find, Get and RefCount are safe from any thread.
 * KickOffRetires, Reclaim and ForEach run exclusively (no concurrent loads).
 * Retire: Release only decrements; KickOffRetires stamps a zero-ref asset once it is no longer loading; Reclaim tears it down when the stamp passes. AddRef clears the stamp.
 * @tparam Id has a uint64_t `id`; 0 is invalid
 */
template<typename T, typename Id, uint32_t Capacity>
class AssetPool
{
    static constexpr size_t MAP_CAPACITY = [] {
        size_t c = 1;
        while (c < static_cast<size_t>(Capacity) * 2) { c <<= 1; }
        return c;
    }();

public:
    using HandleT = Core::Handle<T>;

    struct Acquired
    {
        HandleT handle{HandleT::INVALID};
        T* asset{nullptr};
        bool bCreated{false};
    };

    void Init(Core::VirtualMemoryManager* vm, Core::AllocTag tag, const char* name) { slots.Init(vm, tag, name); }

    /** Takes a ref on id's asset if it is resident. */
    HandleT AddRef(Id id)
    {
        const HandleT handle = Unpack(map.Find(id.id));
        if (!handle.IsValid()) { return HandleT::INVALID; }
        refCounts[handle.index].fetch_add(1, std::memory_order_relaxed);
        retireFrames[handle.index].store(0, std::memory_order_relaxed);
        return handle;
    }

    /**
     * Takes a ref on id's asset, creating it if absent. On creation init(T&, HandleT) runs before any other thread can see the asset.
     * Threads asking for the same id wait until init returns, so init must not block or touch this pool.
     * @return INVALID handle when the pool is full
     */
    template<typename Init>
    Acquired Acquire(Id id, Init&& init)
    {
        assert(id.id != 0);
        for (;;) {
            if (const HandleT existing = AddRef(id); existing.IsValid()) {
                return {existing, slots.Get(existing), false};
            }

            const HandleT handle = slots.Add();
            if (!handle.IsValid()) { return {}; }

            const auto claim = map.TryClaim(id.id);
            if (!claim.bClaimed) {
                slots.Remove(handle);
                if (claim.slot == Core::ConcurrentIdMap<MAP_CAPACITY>::INVALID_SLOT) { return {}; }
                continue;
            }

            ids[handle.index] = id;
            refCounts[handle.index].store(1, std::memory_order_relaxed);
            retireFrames[handle.index].store(0, std::memory_order_relaxed);
            T* asset = slots.Get(handle);
            init(*asset, handle);
            map.Publish(claim.slot, Pack(handle));
            return {handle, asset, true};
        }
    }

    /** @return false if the handle is stale */
    bool Release(HandleT handle)
    {
        if (!slots.IsValid(handle)) { return false; }
        [[maybe_unused]] const uint32_t previous = refCounts[handle.index].fetch_sub(1, std::memory_order_relaxed);
        assert(previous > 0 && "AssetPool: release without a ref");
        return true;
    }

    /** Resident handle for id without taking a ref. */
    HandleT Find(Id id) const { return Unpack(map.Find(id.id)); }

    bool Contains(Id id) const { return map.Find(id.id) != 0; }

    T* Get(HandleT handle) { return slots.Get(handle); }

    const T* Get(HandleT handle) const { return slots.Get(handle); }

    bool IsValid(HandleT handle) const { return slots.IsValid(handle); }

    uint32_t RefCount(HandleT handle) const { return slots.IsValid(handle) ? refCounts[handle.index].load(std::memory_order_relaxed) : 0; }

    uint32_t Count() const { return slots.Count(); }

    /** f(HandleT, T&) for every resident asset. */
    template<typename F>
    void ForEach(F&& f) { slots.ForEach(f); }

    template<typename F>
    void ForEach(F&& f) const { slots.ForEach(f); }

    /** Stamps zero-ref assets for which bSettled(const T&) holds to retire at currentFrame + delay. */
    template<typename Settled>
    void KickOffRetires(uint64_t currentFrame, uint64_t delay, Settled&& bSettled)
    {
        slots.ForEach([&](HandleT handle, const T& asset) {
            if (refCounts[handle.index].load(std::memory_order_relaxed) > 0) { return; }
            if (retireFrames[handle.index].load(std::memory_order_relaxed) != 0) { return; }
            if (bSettled(asset)) { retireFrames[handle.index].store(currentFrame + delay, std::memory_order_relaxed); }
        });
    }

    /**
     * Tears down every stamped asset whose retire frame has passed: teardown(T&, Id), then unmaps and frees the slot.
     * @return number reclaimed
     */
    template<typename Teardown>
    int32_t Reclaim(uint64_t currentFrame, Teardown&& teardown)
    {
        int32_t reclaimed = 0;
        slots.ForEach([&](HandleT handle, T& asset) {
            if (refCounts[handle.index].load(std::memory_order_relaxed) > 0) { return; }
            const uint64_t retireFrame = retireFrames[handle.index].load(std::memory_order_relaxed);
            if (retireFrame == 0 || currentFrame < retireFrame) { return; }

            const Id id = ids[handle.index];
            teardown(asset, id);
            if (Unpack(map.Find(id.id)) == handle) { map.Remove(id.id); }
            ids[handle.index] = {};
            retireFrames[handle.index].store(0, std::memory_order_relaxed);
            slots.Remove(handle);
            ++reclaimed;
        });
        return reclaimed;
    }

private:
    static uint64_t Pack(HandleT handle) { return static_cast<uint64_t>(handle.generation) << 32 | handle.index; }

    static HandleT Unpack(uint64_t value) { return value == 0 ? HandleT::INVALID : HandleT{static_cast<uint32_t>(value), static_cast<uint32_t>(value >> 32)}; }

    Core::ConcurrentSlotPool<T, Capacity> slots;
    Core::ConcurrentIdMap<MAP_CAPACITY> map;
    Core::Array<std::atomic<uint32_t>, Capacity> refCounts{};
    Core::Array<std::atomic<uint64_t>, Capacity> retireFrames{};
    Core::Array<Id, Capacity> ids{};
};
} // Engine

#endif //WILL_ENGINE_ASSET_POOL_H
