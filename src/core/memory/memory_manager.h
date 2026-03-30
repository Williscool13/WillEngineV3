//
// Created by William on 2026-03-30.
//

#ifndef WILL_ENGINE_MEMORY_MANAGER_H
#define WILL_ENGINE_MEMORY_MANAGER_H

#include <cassert>
#include <cstddef>

#include "arena.h"
#include "buddy_allocator.h"

namespace Core
{
/**
 * Top-level memory manager. Performs a single heap allocation at startup and carves it into
 * typed regions. All engine systems suballocate from this manager — no additional new/delete.
 *
 * Layout (contiguous):
 *   [persistentLinear | assetsMeta | assetsPool | physicsMeta | physicsPool]
 *
 * Regions:
 *   - Persistent linear (Arena): bump-pointer, never reset. For engine system objects
 *     (RenderThread, AssetManager, etc.) that live for the entire process lifetime.
 *   - Assets buddy: variable-lifetime allocations for models and textures.
 *   - Physics buddy: variable-lifetime allocations for Jolt rigid bodies and shapes.
 */
class MemoryManager
{
public:
    struct Layout
    {
        size_t persistentLinearSize;
        size_t assetsPoolSize; // must be a power of 2
        size_t assetsMinBlock; // must be a power of 2, >= 8
        size_t physicsPoolSize; // must be a power of 2
        size_t physicsMinBlock; // must be a power of 2, >= 8
    };

    struct Stats
    {
        size_t totalBytes;
        Arena::Stats linear;
        BuddyAllocator::Stats assets;
        BuddyAllocator::Stats physics;
    };

    MemoryManager() = default;

    ~MemoryManager();

    MemoryManager(const MemoryManager&) = delete;

    MemoryManager& operator=(const MemoryManager&) = delete;

    void Init(const Layout& layout);

    /**
     * Bump-allocates sizeof(T) from the persistent linear region and constructs T in-place.
     * Asserts (debug) / returns nullptr (release) on OOM.
     * Never freed individually. Lives for the lifetime of the application.
     */
    template<typename T, typename... Args>
    T* PersistentAlloc(Args&&... args);

    /**
     * Bump-allocates count * sizeof(T) from the persistent linear region.
     * Default-constructs non-trivial elements.
     * Never freed individually. Lives for the lifetime of the application.
     */
    template<typename T>
    T* PersistentAllocArray(size_t count);

    void* PersistentAllocRaw(size_t size, size_t alignment = alignof(std::max_align_t));

    BuddyAllocator& Assets() { return buddyAssets; }
    BuddyAllocator& Physics() { return buddyPhysics; }

    [[nodiscard]] Stats GetStats() const;

private:
    void* megaBuffer{};
    size_t totalSize{};

    Arena persistentArena;
    BuddyAllocator buddyAssets;
    BuddyAllocator buddyPhysics;
};

template<typename T, typename... Args>
T* MemoryManager::PersistentAlloc(Args&&... args)
{
    return persistentArena.Alloc<T>(std::forward<Args>(args)...);
}

template<typename T>
T* MemoryManager::PersistentAllocArray(size_t count)
{
    return persistentArena.AllocArray<T>(count);
}
} // Core

#endif //WILL_ENGINE_MEMORY_MANAGER_H
