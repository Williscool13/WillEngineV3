//
// Created by William on 2026-10-29.
//

#ifndef WILL_ENGINE_CONCURRENT_SLOT_POOL_H
#define WILL_ENGINE_CONCURRENT_SLOT_POOL_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <new>
#include <thread>

#include "core/memory/lock_free_handle_allocator.h"
#include "core/memory/virtual_memory_manager.h"

namespace Core
{
/**
 * Generational slot pool on a VirtualMemoryManager reservation; slots commit one COMMIT_STEP at a time as Add runs dry. Addresses are stable.
 * Add, Remove, Get and IsValid are safe from any thread. ForEach, HandleOf and destruction must not overlap Add or Remove.
 * @tparam HandleTag the handle type is Handle<HandleTag>
 */
template<typename T, uint32_t Capacity, typename HandleTag = T>
class ConcurrentSlotPool
{
    struct Slot
    {
        T value{};
        /** 0 while free. */
        uint32_t generation{0};
    };

    static constexpr uint32_t SLOTS_PER_STEP = (std::max<uint32_t>)(1, static_cast<uint32_t>(VirtualMemoryManager::COMMIT_STEP / sizeof(Slot)));

public:
    using HandleT = Handle<HandleTag>;

    ConcurrentSlotPool() = default;

    ~ConcurrentSlotPool() { Reset(); }

    ConcurrentSlotPool(const ConcurrentSlotPool&) = delete;

    ConcurrentSlotPool& operator=(const ConcurrentSlotPool&) = delete;

    void Init(VirtualMemoryManager* vm_, AllocTag tag, const char* name)
    {
        assert(vm == nullptr && "ConcurrentSlotPool: already initialized");
        vm = vm_;
        reservation = vm->Reserve(static_cast<size_t>(Capacity) * sizeof(Slot), tag, name);
        slots = static_cast<Slot*>(vm->Base(reservation));
    }

    void Reset()
    {
        if (slots == nullptr) { return; }
        const uint32_t constructed = committedSlots.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < constructed; ++i) {
            slots[i].~Slot();
        }
        vm->Release(reservation);
        slots = nullptr;
        committedSlots.store(0, std::memory_order_relaxed);
        publishedSlots.store(0, std::memory_order_relaxed);
    }

    /** @return INVALID when the pool is at Capacity */
    HandleT Add()
    {
        for (;;) {
            const HandleT handle = allocator.Add();
            if (handle.IsValid()) {
                slots[handle.index].generation = handle.generation;
                return handle;
            }
            if (!Grow()) { return HandleT::INVALID; }
        }
    }

    /** Resets the slot to T{} and frees it. Nothing may still read it. */
    bool Remove(HandleT handle)
    {
        if (!allocator.IsValid(handle)) { return false; }
        slots[handle.index].value = T{};
        slots[handle.index].generation = 0;
        return allocator.Remove(handle);
    }

    bool IsValid(HandleT handle) const { return allocator.IsValid(handle); }

    T* Get(HandleT handle) { return allocator.IsValid(handle) ? &slots[handle.index].value : nullptr; }

    const T* Get(HandleT handle) const { return allocator.IsValid(handle) ? &slots[handle.index].value : nullptr; }

    /** handle of a live value in this pool, from its address */
    HandleT HandleOf(const T* value) const
    {
        const auto* bytes = reinterpret_cast<const std::byte*>(value);
        const auto* base = reinterpret_cast<const std::byte*>(&slots[0].value);
        const uint32_t index = static_cast<uint32_t>((bytes - base) / sizeof(Slot));
        assert(index < committedSlots.load(std::memory_order_relaxed) && &slots[index].value == value);
        return {index, slots[index].generation};
    }

    /** f(HandleT, T&) for every live slot. */
    template<typename F>
    void ForEach(F&& f)
    {
        const uint32_t constructed = committedSlots.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < constructed; ++i) {
            if (slots[i].generation != 0) { f(HandleT{i, slots[i].generation}, slots[i].value); }
        }
    }

    template<typename F>
    void ForEach(F&& f) const
    {
        const uint32_t constructed = committedSlots.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < constructed; ++i) {
            if (slots[i].generation != 0) { f(HandleT{i, slots[i].generation}, static_cast<const T&>(slots[i].value)); }
        }
    }

    uint32_t Count() const { return allocator.GetCount(); }

    uint32_t CommittedSlots() const { return committedSlots.load(std::memory_order_relaxed); }

    static constexpr uint32_t GetCapacity() { return Capacity; }

private:
    /** Claims the next step of slots, commits and constructs them, then frees them. @return false at Capacity */
    bool Grow()
    {
        uint32_t begin = committedSlots.load(std::memory_order_acquire);
        if (begin >= Capacity) {
            // Fully claimed is not full: a claimed step is unavailable until its owner frees it into the allocator.
            if (publishedSlots.load(std::memory_order_acquire) >= Capacity) { return false; }
            std::this_thread::yield();
            return true;
        }
        const uint32_t end = (std::min)(begin + SLOTS_PER_STEP, Capacity);
        // Losing the race means another thread is growing; the caller retries Add.
        if (!committedSlots.compare_exchange_strong(begin, end, std::memory_order_acq_rel, std::memory_order_acquire)) { return true; }

        if (!vm->EnsureCommitted(reservation, static_cast<size_t>(end) * sizeof(Slot))) { return false; }
        for (uint32_t i = begin; i < end; ++i) {
            new (&slots[i]) Slot{};
        }
        allocator.Grow(begin, end);
        publishedSlots.fetch_add(end - begin, std::memory_order_acq_rel);
        return true;
    }

    VirtualMemoryManager* vm{nullptr};
    VirtualMemoryManager::Handle reservation{VirtualMemoryManager::INVALID_HANDLE};
    Slot* slots{nullptr};
    std::atomic<uint32_t> committedSlots{0};
    std::atomic<uint32_t> publishedSlots{0};
    LockFreeHandleAllocator<HandleTag, Capacity> allocator{0};
};
} // Core

#endif //WILL_ENGINE_CONCURRENT_SLOT_POOL_H
