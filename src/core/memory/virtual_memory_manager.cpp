//
// Created by William on 2026-08-21.
//

#include "virtual_memory_manager.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

#include "platform/virtual_memory.h"

namespace Core
{
static size_t RoundUp(size_t v, size_t step)
{
    return (v + step - 1) / step * step;
}

VirtualMemoryManager::~VirtualMemoryManager()
{
    for (Entry& e : entries_) {
        if (e.base) {
            Platform::VirtualRelease(e.base);
            e.base = nullptr;
        }
    }
}

VirtualMemoryManager::Handle VirtualMemoryManager::Reserve(size_t bytes, AllocTag tag, const char* name)
{
    std::lock_guard lock(mutex_);

    Handle h = INVALID_HANDLE;
    for (size_t i = 0; i < MAX_RESERVATIONS; ++i) {
        if (entries_[i].base == nullptr) {
            h = static_cast<Handle>(i);
            break;
        }
    }
    if (h == INVALID_HANDLE) {
        fprintf(stderr, "VirtualMemoryManager: reservation table full ('%s')\n", name);
        assert(false && "VirtualMemoryManager: MAX_RESERVATIONS exceeded");
        return INVALID_HANDLE;
    }

    const size_t reserved = RoundUp(bytes, Platform::VirtualReserveGranularity());
    void* base = Platform::VirtualReserve(reserved);
    if (!base) {
        fprintf(stderr, "VirtualMemoryManager: reserve of %zu bytes failed ('%s')\n", reserved, name);
        assert(false && "VirtualMemoryManager: reserve failed");
        return INVALID_HANDLE;
    }

    Entry& e = entries_[h];
    e.base = base;
    e.reserved = reserved;
    e.committed.store(0, std::memory_order_relaxed);
    e.tag = tag;
    e.name = InlineString<32>(name);
    return h;
}

bool VirtualMemoryManager::EnsureCommitted(Handle h, size_t bytes)
{
    Entry& e = entries_[h];
    if (bytes <= e.committed.load(std::memory_order_acquire)) {
        return true;
    }

    std::lock_guard lock(mutex_);
    const size_t committed = e.committed.load(std::memory_order_relaxed);
    if (bytes <= committed) {
        return true;
    }
    if (bytes > e.reserved) {
        fprintf(stderr, "VirtualMemoryManager: '%s' needs %zu bytes, reserved %zu\n", e.name.buf, bytes, e.reserved);
        assert(false && "VirtualMemoryManager: commit past reservation");
        return false;
    }

    const size_t target = std::min(RoundUp(bytes, COMMIT_STEP), e.reserved);
    if (!Platform::VirtualCommit(static_cast<uint8_t*>(e.base) + committed, target - committed)) {
        fprintf(stderr, "VirtualMemoryManager: '%s' commit of %zu bytes failed (committed %zu / %zu)\n", e.name.buf, target - committed, committed, e.reserved);
        assert(false && "VirtualMemoryManager: commit failed");
        return false;
    }
    e.committed.store(target, std::memory_order_release);
    return true;
}

void VirtualMemoryManager::Decommit(Handle h, size_t keepBytes)
{
    std::lock_guard lock(mutex_);
    Entry& e = entries_[h];
    const size_t committed = e.committed.load(std::memory_order_relaxed);
    const size_t keep = std::min(RoundUp(keepBytes, COMMIT_STEP), committed);
    if (keep >= committed) {
        return;
    }
    Platform::VirtualDecommit(static_cast<uint8_t*>(e.base) + keep, committed - keep);
    e.committed.store(keep, std::memory_order_release);
}

void VirtualMemoryManager::Release(Handle h)
{
    std::lock_guard lock(mutex_);
    Entry& e = entries_[h];
    if (e.base) {
        Platform::VirtualRelease(e.base);
    }
    e.base = nullptr;
    e.reserved = 0;
    e.committed.store(0, std::memory_order_relaxed);
    e.tag = AllocTag::Unknown;
    e.name.Clear();
}

void* VirtualMemoryManager::Base(Handle h) const
{
    return entries_[h].base;
}

size_t VirtualMemoryManager::Reserved(Handle h) const
{
    return entries_[h].reserved;
}

size_t VirtualMemoryManager::Committed(Handle h) const
{
    return entries_[h].committed.load(std::memory_order_acquire);
}

VirtualMemoryManager::Stats VirtualMemoryManager::GetStats() const
{
    std::lock_guard lock(mutex_);
    Stats s{};
    for (const Entry& e : entries_) {
        if (!e.base) {
            continue;
        }
        s.reservedBytes += e.reserved;
        s.committedBytes += e.committed.load(std::memory_order_relaxed);
        ++s.reservationCount;
    }
    return s;
}

size_t VirtualMemoryManager::GetReservations(Reservation out[], size_t maxOut) const
{
    std::lock_guard lock(mutex_);
    size_t n = 0;
    for (const Entry& e : entries_) {
        if (!e.base || n >= maxOut) {
            continue;
        }
        out[n++] = Reservation{e.base, e.reserved, e.committed.load(std::memory_order_relaxed), e.tag, e.name};
    }
    return n;
}
} // Core
