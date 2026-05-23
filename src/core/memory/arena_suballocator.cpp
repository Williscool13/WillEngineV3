//
// Created by William on 2026-05-18.
//

#include "arena_suballocator.h"

#include <cassert>

namespace Core
{
void ArenaSuballocator::Init(void* pool, size_t bytes)
{
    tlsf.Init(pool, bytes, false); // mutex_ covers all operations
}

Arena ArenaSuballocator::Acquire(size_t size, AllocTag tag, const char* name)
{
    std::lock_guard lock(mutex_);
    void* chunk = tlsf.Alloc(size, tag);
    if (chunk == nullptr) {
        return Arena{};
    }
    assert(tracked_.Size() < kMaxTracked && "ArenaSuballocator: too many live arenas");
    tracked_.PushBack({chunk, tag, nullptr});
    ++activeChunks_;
    return Arena{chunk, size, name};
}

void ArenaSuballocator::RegisterArena(void* chunkPtr, Arena* arena)
{
    std::lock_guard lock(mutex_);
    for (auto& entry : tracked_) {
        if (entry.chunkPtr == chunkPtr) {
            entry.arena = arena;
            return;
        }
    }
}

void ArenaSuballocator::Release(Arena& arena)
{
    if (arena.Data() == nullptr) {
        return;
    }
    std::lock_guard lock(mutex_);
    tlsf.Free(arena.Data());
    for (size_t i = 0; i < tracked_.Size(); ++i) {
        if (tracked_[i].arena == &arena) {
            tracked_.SwapRemove(i);
            break;
        }
    }
    --activeChunks_;
    arena = Arena{};
}

ArenaSuballocator::Stats ArenaSuballocator::GetStats() const
{
    TlsfAllocator::Stats ts = tlsf.GetStats();
    return {ts.totalBytes, ts.usedBytes, ts.freeBytes, activeChunks_};
}

void ArenaSuballocator::GetTagStats(TlsfAllocator::TagStats out[static_cast<size_t>(AllocTag::Count)])
{
    std::lock_guard lock(mutex_);
    tlsf.GetTagStats(out);
}

size_t ArenaSuballocator::GetLiveArenaStats(LiveArenaStats out[], size_t maxOut)
{
    std::lock_guard lock(mutex_);
    const size_t count = tracked_.Size() < maxOut ? tracked_.Size() : maxOut;
    for (size_t i = 0; i < count; ++i) {
        out[i].tag = tracked_[i].tag;
        out[i].arenaStats = tracked_[i].arena ? tracked_[i].arena->GetStats() : Arena::Stats{};
    }
    return count;
}
} // Core
