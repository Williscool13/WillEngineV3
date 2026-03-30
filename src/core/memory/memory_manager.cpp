//
// Created by William on 2026-03-30.
//

#include "memory_manager.h"

#include <cstdlib>

namespace Core
{
static size_t AlignUp(size_t v, size_t alignment)
{
    return (v + alignment - 1) & ~(alignment - 1);
}

void MemoryManager::Init(const Layout& layout)
{
    assert(megaBuffer == nullptr && "MemoryManager::Init called twice");
    assert((layout.assetsPoolSize  & (layout.assetsPoolSize  - 1)) == 0 && "assetsPoolSize must be a power of 2");
    assert((layout.assetsMinBlock  & (layout.assetsMinBlock  - 1)) == 0 && "assetsMinBlock must be a power of 2");
    assert((layout.physicsPoolSize & (layout.physicsPoolSize - 1)) == 0 && "physicsPoolSize must be a power of 2");
    assert((layout.physicsMinBlock & (layout.physicsMinBlock - 1)) == 0 && "physicsMinBlock must be a power of 2");

    constexpr size_t kAlign = alignof(std::max_align_t);

    const size_t linearSz     = AlignUp(layout.persistentLinearSize, kAlign);
    const size_t assetsMetaSz = AlignUp(BuddyAllocator::MetadataSize(layout.assetsPoolSize, layout.assetsMinBlock), kAlign);
    const size_t physMetaSz   = AlignUp(BuddyAllocator::MetadataSize(layout.physicsPoolSize, layout.physicsMinBlock), kAlign);

    totalSize = linearSz + assetsMetaSz + layout.assetsPoolSize + physMetaSz + layout.physicsPoolSize;

    megaBuffer = malloc(totalSize);
    assert(megaBuffer != nullptr && "MemoryManager: mega allocation failed");

    auto* cursor = static_cast<uint8_t*>(megaBuffer);

    persistentArena = Arena(cursor, linearSz);
    cursor += linearSz;

    void* assetsMeta = cursor; cursor += assetsMetaSz;
    void* assetsPool = cursor; cursor += layout.assetsPoolSize;
    buddyAssets.Init(assetsPool, layout.assetsPoolSize, layout.assetsMinBlock, assetsMeta);

    void* physMeta = cursor; cursor += physMetaSz;
    void* physPool = cursor;
    buddyPhysics.Init(physPool, layout.physicsPoolSize, layout.physicsMinBlock, physMeta);
}

MemoryManager::~MemoryManager()
{
    free(megaBuffer);
    megaBuffer = nullptr;
}

void* MemoryManager::PersistentAllocRaw(size_t size, size_t alignment)
{
    return persistentArena.AllocRaw(size, alignment);
}

MemoryManager::Stats MemoryManager::GetStats() const
{
    return {
        totalSize,
        persistentArena.GetStats(),
        buddyAssets.GetStats(),
        buddyPhysics.GetStats(),
    };
}
} // Core
