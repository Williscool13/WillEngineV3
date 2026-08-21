//
// Created by William on 2026-03-30.
//

#ifndef WILL_ENGINE_MEMORY_MANAGER_H
#define WILL_ENGINE_MEMORY_MANAGER_H

#include <cstdint>

#include <atomic>

#include "arena.h"
#include "arena_suballocator.h"
#include "tlsf_allocator.h"
#include "virtual_memory_manager.h"

namespace Core
{
/**
 * Top-level memory manager. Performs a single heap allocation at startup and carves it into
 * typed regions. All engine systems suballocate from this manager — no additional new/delete.
 *
 * Layout (contiguous):
 *   [persistentPool | physicsPool | arenaPool]  (general/assets/assetsScratch/render/vulkan are growable, own their chunks)
 *
 * Regions:
 *   - Persistent TLSF: individual allocs that live for the entire process lifetime.
 *     For engine system objects (RenderThread, AssetManager, etc.).
 *   - General TLSF: variable-lifetime allocations with no specific domain.
 *   - Assets TLSF: variable-lifetime allocations for models and textures.
 *   - Physics TLSF: variable-lifetime allocations for Jolt rigid bodies and shapes.
 *   - Physics Arena: contiguous scratch buffer for Jolt's per-step TempAllocator; reset each step.
 *   - Render TLSF: variable-lifetime allocations for render system objects.
 *   - Render Arena: per-frame bump allocator for transient render data; Reset() each frame.
 *   - General Arena: per-frame bump allocator for transient render data; Reset() each frame.
 */
class MemoryManager
{
    // todo make struct. remove getters
public:
    struct Layout
    {
        size_t persistentSize;
        size_t physicsPoolSize;
        size_t arenaPoolSize;

        // Growables (heap allocated)
        size_t generalPoolSize;
        size_t generalPoolBudget;
        size_t assetsPoolSize;
        size_t assetsPoolBudget;
        size_t assetsScratchPoolSize;
        size_t assetsScratchBudget;
        size_t renderPoolSize;
        size_t renderPoolBudget;
        size_t vulkanPoolSize;
        size_t vulkanPoolBudget;
    };

    struct Stats
    {
        size_t totalBytes;
        TlsfAllocator::Stats persistent;
        TlsfAllocator::Stats general;
        TlsfAllocator::Stats assetsScratch;
        TlsfAllocator::Stats assets;
        TlsfAllocator::Stats physics;
        TlsfAllocator::Stats render;
        TlsfAllocator::Stats vulkan;
        VirtualMemoryManager::Stats virtualMemory;

        struct
        {
            uint32_t allocationCount;
            uint64_t totalBytes;
        } deviceMemory;
    };

    MemoryManager() = default;

    ~MemoryManager();

    MemoryManager(const MemoryManager&) = delete;

    MemoryManager& operator=(const MemoryManager&) = delete;

    void Init(const Layout& layout);

    void* PersistentAllocRaw(size_t size, AllocTag tag = AllocTag::Unknown);

    void* PersistentRealloc(void* ptr, size_t newSize, AllocTag tag = AllocTag::Unknown);

    void PersistentFree(void* ptr);

    void* GeneralAllocRaw(size_t size, AllocTag tag = AllocTag::Unknown);

    void* GeneralRealloc(void* ptr, size_t newSize, AllocTag tag = AllocTag::Unknown);

    void GeneralFree(void* ptr);

    void* PhysicsAllocRaw(size_t size, size_t alignment);

    void* PhysicsRealloc(void* ptr, size_t newSize, size_t alignment);

    void PhysicsFree(void* ptr);

    void* RenderAllocRaw(size_t size);

    void* RenderRealloc(void* ptr, size_t newSize);

    void RenderFree(void* ptr);

    TlsfAllocator& Persistent() { return tlsfPersistent; }
    TlsfAllocator& General() { return tlsfGeneral; }
    TlsfAllocator& AssetsScratch() { return tlsfAssetsScratch; }
    TlsfAllocator& Assets() { return tlsfAssets; }
    TlsfAllocator& Physics() { return tlsfPhysics; }
    TlsfAllocator& Render() { return tlsfRender; }
    TlsfAllocator& Vulkan() { return tlsfVulkan; }
    /**
     * General per-frame arena. Cleared at the end of each game frame.
     * Access from game thread only.
     * @return
     */
    ArenaSuballocator& ArenaPool() { return arenaPool; }
    VirtualMemoryManager& Virtual() { return virtualMemory; }

    [[nodiscard]] Stats GetStats();

    void TrackDeviceAlloc(uint64_t size);

    void TrackDeviceFree(uint64_t size);

private:
    void* megaBuffer{};
    size_t totalSize{};

    TlsfAllocator tlsfPersistent;
    TlsfAllocator tlsfGeneral;

    TlsfAllocator tlsfAssetsScratch;
    TlsfAllocator tlsfAssets;
    TlsfAllocator tlsfPhysics;
    TlsfAllocator tlsfRender;
    TlsfAllocator tlsfVulkan;
    ArenaSuballocator arenaPool;
    VirtualMemoryManager virtualMemory;

    std::atomic<uint32_t> deviceAllocCount{0};
    std::atomic<uint64_t> deviceAllocBytes{0};
};

} // Core

#endif //WILL_ENGINE_MEMORY_MANAGER_H
