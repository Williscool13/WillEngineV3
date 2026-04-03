//
// Created by William on 2026-03-30.
//

#ifndef WILL_ENGINE_MEMORY_MANAGER_H
#define WILL_ENGINE_MEMORY_MANAGER_H

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>

#include "arena.h"
#include "tlsf_allocator.h"

namespace Core
{
/**
 * Top-level memory manager. Performs a single heap allocation at startup and carves it into
 * typed regions. All engine systems suballocate from this manager — no additional new/delete.
 *
 * Layout (contiguous):
 *   [persistentPool | generalPool | assetsPool | physicsPool | renderPool | renderArena]
 *
 * Regions:
 *   - Persistent TLSF: individual allocs that live for the entire process lifetime.
 *     For engine system objects (RenderThread, AssetManager, etc.).
 *   - General TLSF: variable-lifetime allocations with no specific domain.
 *   - Assets TLSF: variable-lifetime allocations for models and textures.
 *   - Physics TLSF: variable-lifetime allocations for Jolt rigid bodies and shapes.
 *   - Render TLSF: variable-lifetime allocations for render system objects.
 *   - Render Arena: per-frame bump allocator for transient render data; Reset() each frame.
 */
class MemoryManager
{
public:
    struct Layout
    {
        size_t persistentSize;
        size_t generalPoolSize;
        size_t assetsPoolSize;
        size_t physicsPoolSize;
        size_t renderPoolSize;
        size_t renderArenaSize;
    };

    struct Stats
    {
        size_t totalBytes;
        TlsfAllocator::Stats persistent;
        TlsfAllocator::Stats general;
        TlsfAllocator::Stats assets;
        TlsfAllocator::Stats physics;
        TlsfAllocator::Stats render;
        // todo render arena
    };

    MemoryManager() = default;
    ~MemoryManager();

    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    void Init(const Layout& layout);

    /**
     * Allocates sizeof(T) from the persistent pool and constructs T in-place.
     * Not freed individually. Lives for the lifetime of the application.
     */
    template<typename T, typename... Args>
    T* PersistentAlloc(Args&&... args);

    /**
     * Allocates count * sizeof(T) from the persistent pool.
     * Default-constructs non-trivial elements.
     * Not freed individually. Lives for the lifetime of the application.
     */
    template<typename T>
    T* PersistentAllocArray(size_t count);

    void* PersistentAllocRaw(size_t size);
    void* GeneralAllocRaw(size_t size, AllocTag tag = AllocTag::Unknown);
    void* GeneralRealloc(void* ptr, size_t newSize, AllocTag tag = AllocTag::Unknown);
    void  GeneralFree(void* ptr);

    void* RenderAllocRaw(size_t size);
    void* RenderRealloc(void* ptr, size_t newSize);
    void  RenderFree(void* ptr);

    TlsfAllocator& Persistent() { return tlsfPersistent; }
    TlsfAllocator& General()   { return tlsfGeneral; }
    TlsfAllocator& Assets()    { return tlsfAssets; }
    TlsfAllocator& Physics()   { return tlsfPhysics; }
    TlsfAllocator& Render()    { return tlsfRender; }
    Arena&         RenderArena() { return renderArena; }

    [[nodiscard]] Stats GetStats() const;

private:
    void*  megaBuffer{};
    size_t totalSize{};

    TlsfAllocator tlsfPersistent;
    TlsfAllocator tlsfGeneral;
    TlsfAllocator tlsfAssets;
    TlsfAllocator tlsfPhysics;
    TlsfAllocator tlsfRender;
    Arena         renderArena;
};

template<typename T, typename... Args>
T* MemoryManager::PersistentAlloc(Args&&... args)
{
    void* ptr = tlsfPersistent.Alloc(sizeof(T), AllocTag::Persistent);
    assert(ptr != nullptr && "OOM: persistent pool exhausted");
    return new(ptr) T(std::forward<Args>(args)...);
}

template<typename T>
T* MemoryManager::PersistentAllocArray(size_t count)
{
    assert(count > 0);
    void* ptr = tlsfPersistent.Alloc(sizeof(T) * count, AllocTag::Persistent);
    assert(ptr != nullptr && "OOM: persistent pool exhausted");
    if constexpr (!std::is_trivially_constructible_v<T>) {
        T* arr = static_cast<T*>(ptr);
        for (size_t i = 0; i < count; ++i) { new(arr + i) T(); }
    }
    return static_cast<T*>(ptr);
}
} // Core

#endif //WILL_ENGINE_MEMORY_MANAGER_H
