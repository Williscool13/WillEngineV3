//
// Created by William on 2026-03-29.
//

#include "arena.h"

#include <cassert>
#include <cstdio>

#include "virtual_memory_manager.h"

namespace Core
{
Arena::Arena(void* memory, size_t size, const char* name)
    : memory(memory), head(0), capacity(size), committed(size), name(name)
{
    assert(memory != nullptr);
    assert(size > 0);
}

Arena::Arena(void* memory, size_t size, const char* name, VirtualMemoryManager* vm, uint32_t vmHandle)
    : memory(memory), head(0), capacity(size), committed(vm->Committed(vmHandle)), vm(vm), vmHandle(vmHandle), name(name)
{
    assert(memory != nullptr);
    assert(size > 0);
}

Arena::Arena(Arena&& other) noexcept
    : memory(other.memory),
      head(other.head.load(std::memory_order_relaxed)),
      capacity(other.capacity),
      committed(other.committed.load(std::memory_order_relaxed)),
      vm(other.vm),
      vmHandle(other.vmHandle),
      peakHead(other.peakHead.load(std::memory_order_relaxed)),
      name(other.name)
{
}

Arena& Arena::operator=(Arena&& other) noexcept
{
    if (this != &other) {
        memory = other.memory;
        head.store(other.head.load(std::memory_order_relaxed), std::memory_order_relaxed);
        capacity = other.capacity;
        committed.store(other.committed.load(std::memory_order_relaxed), std::memory_order_relaxed);
        vm = other.vm;
        vmHandle = other.vmHandle;
        peakHead.store(other.peakHead.load(std::memory_order_relaxed), std::memory_order_relaxed);
        name = other.name;
    }
    return *this;
}

void* Arena::AllocRaw(size_t size, size_t alignment)
{
    assert(size > 0);
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be a power of two");

    size_t oldHead = head.load(std::memory_order_relaxed);
    size_t alignedHead;
    size_t newHead;
    do {
        alignedHead = (oldHead + alignment - 1) & ~(alignment - 1);
        newHead = alignedHead + size;
        if (newHead > capacity) {
            fprintf(stderr, "Arena '%s' OOM: Alloc %zu bytes (align %zu), used %zu / %zu\n", name.buf, size, alignment, oldHead, capacity);
            assert(false && "Arena out of memory");
            return nullptr;
        }
    } while (!head.compare_exchange_weak(oldHead, newHead, std::memory_order_relaxed));

    size_t prevPeak = peakHead.load(std::memory_order_relaxed);
    while (newHead > prevPeak && !peakHead.compare_exchange_weak(prevPeak, newHead, std::memory_order_relaxed)) {
    }

    if (newHead > committed.load(std::memory_order_acquire)) {
        std::lock_guard lock(commitMutex);
        if (newHead > committed.load(std::memory_order_relaxed)) {
            vm->EnsureCommitted(vmHandle, newHead);
            committed.store(vm->Committed(vmHandle), std::memory_order_release);
        }
    }
    return static_cast<char*>(memory) + alignedHead;
}

void Arena::Reset()
{
    head.store(0, std::memory_order_relaxed);
}

void Arena::Trim(size_t keepBytes)
{
    if (!vm) { return; }
    assert(head.load(std::memory_order_relaxed) <= keepBytes);
    vm->Decommit(vmHandle, keepBytes);
    committed.store(vm->Committed(vmHandle), std::memory_order_relaxed);
}
} // Core
