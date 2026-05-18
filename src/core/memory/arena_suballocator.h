//
// Created by William on 2026-05-18.
//

#ifndef WILL_ENGINE_ARENA_SUBALLOCATOR_H
#define WILL_ENGINE_ARENA_SUBALLOCATOR_H

#include <cstddef>
#include <utility>

#include "arena.h"
#include "tlsf_allocator.h"

namespace Core
{
/**
 * TLSF-backed pool from which callers can acquire Arena-wrapped chunks.
 *
 * Usage:
 *   ArenaSuballocator sub;
 *   sub.Init(pool, poolBytes, false);
 *
 *   Arena a = sub.Acquire(256 * 1024, AllocTag::FrameSync);
 *   // ... bump-allocate into a ...
 *   sub.Release(a);  // chunk returned to pool; a is invalidated
 */
class ArenaSuballocator
{
public:
    void Init(void* pool, size_t bytes, bool bUseMutex);

    /**
     * Allocates a contiguous chunk of `size` bytes from the pool and returns
     * an Arena wrapping it. Returns a null Arena (Data() == nullptr) on failure.
     */
    Arena Acquire(size_t size, AllocTag tag = AllocTag::Unknown);

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

    [[nodiscard]] Stats GetStats() const;

    void GetTagStats(TlsfAllocator::TagStats out[static_cast<size_t>(AllocTag::Count)]) { tlsf.GetTagStats(out); }

private:
    TlsfAllocator tlsf;
    size_t activeChunks_{0};
};
/**
 * RAII owner of an Arena acquired from an ArenaSuballocator.
 * Releases the chunk back to the pool on destruction.
 */
class ManagedArena
{
public:
    ManagedArena() = default;

    ManagedArena(ArenaSuballocator& pool, size_t size, AllocTag tag = AllocTag::Unknown)
        : pool_(&pool), arena_(pool.Acquire(size, tag)) {}

    ~ManagedArena() { Release(); }

    ManagedArena(const ManagedArena&) = delete;
    ManagedArena& operator=(const ManagedArena&) = delete;

    ManagedArena(ManagedArena&& other) noexcept
        : pool_(other.pool_), arena_(std::move(other.arena_))
    {
        other.pool_ = nullptr;
    }

    ManagedArena& operator=(ManagedArena&& other) noexcept
    {
        if (this != &other) {
            Release();
            pool_ = other.pool_;
            arena_ = std::move(other.arena_);
            other.pool_ = nullptr;
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
