//
// Created by William on 2026-03-31.
//

#ifndef WILL_ENGINE_TLSF_ALLOCATOR_H
#define WILL_ENGINE_TLSF_ALLOCATOR_H

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace Core
{
enum class AllocTag : uint32_t
{
    Unknown = 0,
    // Assets
    AssetModel,
    AssetTexture,
    AssetGenerator,
    // Physics
    Physics,
    // Render
    RenderMesh,
    RenderMaterial,
    Render,
    // ECS
    ECS,
    // Scheduler
    TaskScheduler,
    // SDL
    SDL,
    // ImGui
    ImGui,
    Editor,
    // Engine systems
    EngineLogger,
    EngineContext,
    EngineState,
    InputManager,
    TimeManager,
    FrameSync,
    RenderThread,
    AudioManager,
    AsyncAssetLoadManager,
    AssetManager,
    MaterialManager,
    Clay,

    Count
};

const char* AllocTagName(AllocTag tag);

/**
 * TLSF (Two-Level Segregated Fit) allocator wrapper.
 * Suballocates from an externally-provided buffer with O(1) alloc/free.
 *
 * Every allocation is prefixed with a 16-byte AllocHeader storing an AllocTag
 * and the user-requested size.
 *
 * Usage:
 *   TlsfAllocator alloc;
 *   alloc.Init(pool, poolBytes);
 *   void* p = alloc.Alloc(256, AllocTag::AssetModel);
 *   alloc.Free(p);
 */
class TlsfAllocator
{
public:
    // Bytes added to every allocation for the tag header.
    // Must equal alignof(std::max_align_t) so returned pointers stay max-aligned.
    static constexpr size_t kHeaderSize = 16;

    void Init(void* pool, size_t bytes, bool bUseMutex);

    void* Alloc(size_t size, AllocTag tag = AllocTag::Unknown);

    void* Realloc(void* ptr, size_t newSize, AllocTag tag = AllocTag::Unknown);

    void Free(void* ptr);

    // No AllocHeader; tag stats reported under AllocTag::Aligned.
    // usedBytes_ uses tlsf_block_size for symmetric tracking.
    void* AlignedAlloc(size_t size, size_t alignment, AllocTag tag = AllocTag::Unknown);

    void AlignedFree(void* ptr);

    struct Stats
    {
        size_t totalBytes;
        size_t usedBytes; // headers + user data for live allocations
        size_t freeBytes;
        size_t allocCount;
    };

    struct TagStats
    {
        AllocTag tag;
        size_t count;
        size_t usedBytes; // sum of user-requested sizes (excludes header overhead)
    };

    [[nodiscard]] Stats GetStats() const;

    // Walks the pool and fills out[0..(Count-1)] with per-tag aggregates, indexed by tag value.
    // Always fills exactly AllocTag::Count entries.
    void GetTagStats(TagStats out[static_cast<size_t>(AllocTag::Count)]);

private:
    struct AllocHeader
    {
        AllocTag tag; // 4 bytes
        uint32_t size; // 4 bytes — user-requested size
        uint64_t _pad; // 8 bytes
    };

    static_assert(sizeof(AllocHeader) == 16, "AllocHeader must be 16 bytes");

    struct TagWalkCtx
    {
        TagStats* out; // array of AllocTag::Count entries, indexed by tag value
    };

    static void TagWalker(void* ptr, size_t size, int used, void* user);

    void* tlsf{};
    size_t poolBytes{};
    size_t usedBytes_{};
    size_t allocCount_{};
    std::mutex mutex_;
    bool bUseMutex_{false};
};
} // Core

#endif //WILL_ENGINE_TLSF_ALLOCATOR_H
