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

    constexpr size_t kAlign = alignof(std::max_align_t);

    const size_t linearSz  = AlignUp(layout.persistentLinearSize, kAlign);
    const size_t generalSz = AlignUp(layout.generalPoolSize, kAlign);
    const size_t assetsSz  = AlignUp(layout.assetsPoolSize, kAlign);
    const size_t physicsSz = AlignUp(layout.physicsPoolSize, kAlign);

    totalSize = linearSz + generalSz + assetsSz + physicsSz;

    megaBuffer = malloc(totalSize);
    assert(megaBuffer != nullptr && "MemoryManager: mega allocation failed");

    auto* cursor = static_cast<uint8_t*>(megaBuffer);

    persistentArena = Arena(cursor, linearSz);
    cursor += linearSz;

    tlsfGeneral.Init(cursor, generalSz); cursor += generalSz;
    tlsfAssets.Init(cursor, assetsSz);   cursor += assetsSz;
    tlsfPhysics.Init(cursor, physicsSz);
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
        tlsfGeneral.GetStats(),
        tlsfAssets.GetStats(),
        tlsfPhysics.GetStats(),
    };
}
} // Core
