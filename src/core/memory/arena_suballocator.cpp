//
// Created by William on 2026-05-18.
//

#include "arena_suballocator.h"

namespace Core
{
void ArenaSuballocator::Init(void* pool, size_t bytes, bool bUseMutex)
{
    tlsf.Init(pool, bytes, bUseMutex);
}

Arena ArenaSuballocator::Acquire(size_t size, AllocTag tag)
{
    void* chunk = tlsf.Alloc(size, tag);
    if (chunk == nullptr) {
        return Arena{};
    }
    ++activeChunks_;
    return Arena{chunk, size};
}

void ArenaSuballocator::Release(Arena& arena)
{
    if (arena.Data() == nullptr) {
        return;
    }
    tlsf.Free(arena.Data());
    --activeChunks_;
    arena = Arena{};
}

ArenaSuballocator::Stats ArenaSuballocator::GetStats() const
{
    TlsfAllocator::Stats ts = tlsf.GetStats();
    return {ts.totalBytes, ts.usedBytes, ts.freeBytes, activeChunks_};
}
} // Core
