//
// Created by William on 2026-05-18.
//

#ifndef WILL_ENGINE_ARENA_SUBALLOCATOR_H
#define WILL_ENGINE_ARENA_SUBALLOCATOR_H

#include <cstddef>
#include <mutex>
#include <utility>

#include "arena.h"
#include "tlsf_allocator.h"
#include "core/containers/inline_vector.h"

namespace Core
{
/**
 * TLSF-backed pool from which callers can acquire Arena-wrapped chunks.
 * Thread-safe: a single mutex covers Acquire, Release, and stats queries.
 *
 * Usage:
 *   ArenaSuballocator sub;
 *   sub.Init(pool, poolBytes, "MyPool");
 *
 *   Arena a = sub.Acquire(256 * 1024, AllocTag::FrameSync);
 *   sub.RegisterArena(a.Data(), &a);   // patch live pointer for stats
 *   // ... bump-allocate into a ...
 *   sub.Release(a);  // chunk returned to pool; a is invalidated
 */
class ArenaSuballocator
{
public:
    static constexpr size_t kMaxTracked = 64;

    void Init(void* pool, size_t bytes, const char* name);

    /**
     * Allocates a contiguous chunk of `size` bytes from the pool and returns
     * an Arena wrapping it. Returns a null Arena (Data() == nullptr) on failure.
     * Call RegisterArena(arena.Data(), &arena) afterwards to enable live stats.
     */
    Arena Acquire(size_t size, AllocTag tag = AllocTag::Unknown, const char* name = "");

    /**
     * Patches the Arena* for an already-acquired chunk so GetLiveArenaStats can read it.
     * chunkPtr must equal the arena.Data() returned by Acquire.
     */
    void RegisterArena(void* chunkPtr, Arena* arena);

    /**
     * Returns the chunk backing `arena` to the pool and invalidates `arena`.
     * No-op if arena.Data() is nullptr.
     */
    void Release(Arena& arena);

    struct Stats
    {
        size_t totalBytes;
        size_t usedBytes;
        size_t freeBytes;
        size_t activeChunks;
    };

    struct LiveArenaStats
    {
        AllocTag tag;
        Arena::Stats arenaStats;
    };

    [[nodiscard]] Stats GetStats() const;

    [[nodiscard]] const char* GetName() const { return tlsf.GetName(); }

    void GetTagStats(TlsfAllocator::TagStats out[static_cast<size_t>(AllocTag::Count)]);

    /** Locks and snapshots peak/used/capacity for all live arenas. Returns count written. */
    size_t GetLiveArenaStats(LiveArenaStats out[], size_t maxOut);

private:
    struct TrackedArena
    {
        void* chunkPtr{nullptr};
        AllocTag tag{AllocTag::Unknown};
        Arena* arena{nullptr};
    };

    TlsfAllocator tlsf;
    size_t activeChunks_{0};
    mutable std::mutex mutex_;

    InlineVector<TrackedArena, kMaxTracked> tracked_;
};

/**
 * RAII owner of an Arena acquired from an ArenaSuballocator.
 * Releases the chunk back to the pool on destruction.
 */
class ManagedArena
{
public:
    ManagedArena() = default;

    ManagedArena(ArenaSuballocator& pool, size_t size, AllocTag tag = AllocTag::Unknown, const char* name = "")
        : pool_(&pool), arena_(pool.Acquire(size, tag, name))
    {
        if (arena_.Data()) {
            pool.RegisterArena(arena_.Data(), &arena_);
        }
    }

    ~ManagedArena() { Release(); }

    ManagedArena(const ManagedArena&) = delete;
    ManagedArena& operator=(const ManagedArena&) = delete;

    ManagedArena(ManagedArena&& other) noexcept
        : pool_(other.pool_), arena_(std::move(other.arena_))
    {
        other.pool_ = nullptr;
        if (pool_ && arena_.Data()) {
            pool_->RegisterArena(arena_.Data(), &arena_);
        }
    }

    ManagedArena& operator=(ManagedArena&& other) noexcept
    {
        if (this != &other) {
            Release();
            pool_ = other.pool_;
            arena_ = std::move(other.arena_);
            other.pool_ = nullptr;
            if (pool_ && arena_.Data()) {
                pool_->RegisterArena(arena_.Data(), &arena_);
            }
        }
        return *this;
    }

    void Release()
    {
        if (pool_ && arena_.Data() != nullptr) {
            pool_->Release(arena_);
            pool_ = nullptr;
        }
    }

    [[nodiscard]] Arena& Get() { return arena_; }
    [[nodiscard]] const Arena& Get() const { return arena_; }
    [[nodiscard]] bool IsValid() const { return arena_.Data() != nullptr; }

private:
    ArenaSuballocator* pool_{nullptr};
    Arena arena_{};
};
} // Core

#endif //WILL_ENGINE_ARENA_SUBALLOCATOR_H
