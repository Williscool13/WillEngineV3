//
// Created by William on 2026-03-30.
//

#include "memory_manager.h"

namespace Core
{
static size_t AlignUp(size_t v, size_t alignment)
{
    return (v + alignment - 1) & ~(alignment - 1);
}

void MemoryManager::Init(const Layout& layout)
{
    assert(megaBuffer == nullptr && "MemoryManager::Init called twice");

    constexpr size_t kAlign = 64;

    const size_t persistentSz = AlignUp(layout.persistentSize, kAlign);
    const size_t generalSz = AlignUp(layout.generalPoolSize, kAlign);
    const size_t assetsScratchSz = AlignUp(layout.assetsScratchPoolSize, kAlign);
    const size_t assetsSz = AlignUp(layout.assetsPoolSize, kAlign);
    const size_t physicsAlignedSz = AlignUp(layout.physicsAlignedPoolSize, kAlign);
    const size_t renderSz = AlignUp(layout.renderPoolSize, kAlign);
    const size_t arenaPoolSz = AlignUp(layout.arenaPoolSize, kAlign);

    totalSize = persistentSz + generalSz + assetsScratchSz + assetsSz + physicsAlignedSz + renderSz + arenaPoolSz;

    megaBuffer = malloc(totalSize);
    assert(megaBuffer != nullptr && "MemoryManager: mega allocation failed");

    auto* cursor = static_cast<uint8_t*>(megaBuffer);

    tlsfPersistent.Init(cursor, persistentSz, true);
    cursor += persistentSz;
    tlsfGeneral.Init(cursor, generalSz, true);
    cursor += generalSz;
    tlsfAssets.Init(cursor, assetsSz, true);
    cursor += assetsSz;
    tlsfAssetsScratch.Init(cursor, assetsScratchSz, true);
    cursor += assetsScratchSz;
    tlsfPhysicsAligned.Init(cursor, physicsAlignedSz, true);
    cursor += physicsAlignedSz;
    tlsfRender.Init(cursor, renderSz, false);
    cursor += renderSz;
    arenaPool.Init(cursor, arenaPoolSz);
}

MemoryManager::~MemoryManager()
{
    free(megaBuffer);
    megaBuffer = nullptr;
}

void* MemoryManager::PersistentAllocRaw(size_t size, AllocTag tag)
{
    void* ptr = tlsfPersistent.Alloc(size, tag);
    assert(ptr != nullptr && "OOM: persistent pool exhausted");
    return ptr;
}

void* MemoryManager::PersistentRealloc(void* ptr, size_t newSize, AllocTag tag)
{
    void* p = tlsfPersistent.Realloc(ptr, newSize, tag);
    assert(p != nullptr && "OOM: persistent pool exhausted");
    return p;
}

void MemoryManager::PersistentFree(void* ptr)
{
    tlsfPersistent.Free(ptr);
}

void* MemoryManager::GeneralAllocRaw(size_t size, AllocTag tag)
{
    void* ptr = tlsfGeneral.Alloc(size, tag);
    assert(ptr != nullptr && "OOM: general pool exhausted");
    return ptr;
}

void* MemoryManager::GeneralRealloc(void* ptr, size_t newSize, AllocTag tag)
{
    void* p = tlsfGeneral.Realloc(ptr, newSize, tag);
    assert(p != nullptr && "OOM: general pool exhausted");
    return p;
}

void MemoryManager::GeneralFree(void* ptr)
{
    tlsfGeneral.Free(ptr);
}

void* MemoryManager::PhysicsAlignedAllocRaw(size_t size, size_t alignment)
{
    void* ptr = tlsfPhysicsAligned.AlignedAlloc(size, alignment);
    assert(ptr != nullptr && "OOM: physics aligned pool exhausted");
    return ptr;
}

void MemoryManager::PhysicsAlignedFree(void* ptr)
{
    tlsfPhysicsAligned.AlignedFree(ptr);
}

void* MemoryManager::RenderAllocRaw(size_t size)
{
    void* ptr = tlsfRender.Alloc(size, AllocTag::Render);
    assert(ptr != nullptr && "OOM: render pool exhausted");
    return ptr;
}

void* MemoryManager::RenderRealloc(void* ptr, size_t newSize)
{
    void* p = tlsfRender.Realloc(ptr, newSize, AllocTag::Render);
    assert(p != nullptr && "OOM: render pool exhausted");
    return p;
}

void MemoryManager::RenderFree(void* ptr)
{
    tlsfRender.Free(ptr);
}

MemoryManager::Stats MemoryManager::GetStats()
{
    Stats s{
        totalSize,
        tlsfPersistent.GetStats(),
        tlsfGeneral.GetStats(),
        tlsfAssetsScratch.GetStats(),
        tlsfAssets.GetStats(),
        tlsfPhysicsAligned.GetStats(),
        tlsfRender.GetStats(),
    };
#ifdef WDEBUG
    s.deviceMemory.allocationCount = deviceAllocCount.load(std::memory_order_relaxed);
    s.deviceMemory.totalBytes      = deviceAllocBytes.load(std::memory_order_relaxed);
#endif
    return s;
}

void MemoryManager::TrackDeviceAlloc(uint64_t size)
{
#ifdef WDEBUG
    deviceAllocCount.fetch_add(1, std::memory_order_relaxed);
    deviceAllocBytes.fetch_add(size, std::memory_order_relaxed);
#endif
}

void MemoryManager::TrackDeviceFree(uint64_t size)
{
#ifdef WDEBUG
    deviceAllocCount.fetch_sub(1, std::memory_order_relaxed);
    deviceAllocBytes.fetch_sub(size, std::memory_order_relaxed);
#endif
}
} // Core
