//
// Created by William on 2026-03-31.
//

#include "tlsf_allocator.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "tlsf.h"

namespace Core
{
static constexpr size_t GROW_CHUNK_BYTES = 256ull * 1024 * 1024;

static void UsedBlockWalker(void* /*ptr*/, size_t /*size*/, int used, void* user)
{
    if (used) { *static_cast<bool*>(user) = true; }
}

const char* AllocTagName(AllocTag tag)
{
    switch (tag) {
        case AllocTag::Unknown: return "Unknown";
        case AllocTag::AssetModel: return "AssetModel";
        case AllocTag::AssetTexture: return "AssetTexture";
        case AllocTag::AssetGenerator: return "AssetGenerator";
        case AllocTag::Physics: return "Physics";
        case AllocTag::RenderMesh: return "RenderMesh";
        case AllocTag::RenderMaterial: return "RenderMaterial";
        case AllocTag::Render: return "Render";
        case AllocTag::ECS: return "ECS";
        case AllocTag::TaskScheduler: return "TaskScheduler";
        case AllocTag::SDL: return "SDL";
        case AllocTag::ImGui: return "ImGui";
        case AllocTag::Editor: return "Editor";
        case AllocTag::EngineLogger: return "EngineLogger";
        case AllocTag::EngineContext: return "EngineContext";
        case AllocTag::EngineState: return "EngineState";
        case AllocTag::InputManager: return "InputManager";
        case AllocTag::TimeManager: return "TimeManager";
        case AllocTag::FrameSync0: return "FrameSync0";
        case AllocTag::FrameSync1: return "FrameSync1";
        case AllocTag::FrameSync2: return "FrameSync2";
        case AllocTag::FrameSync3: return "FrameSync3";
        case AllocTag::RenderThread: return "RenderThread";
        case AllocTag::AudioManager: return "AudioManager";
        case AllocTag::AsyncAssetLoadManager: return "AsyncAssetLoadManager";
        case AllocTag::AssetManager: return "AssetManager";
        case AllocTag::MaterialManager: return "MaterialManager";
        case AllocTag::Clay: return "Clay";
        case AllocTag::Count: return "Count";
    }
    return "Unknown";
}

void TlsfAllocator::Init(void* pool, size_t bytes, bool bUseMutex, const char* name)
{
    assert(pool != nullptr);
    assert(bytes > tlsf_size() && "pool too small for TLSF control structure");
    tlsf = tlsf_create_with_pool(pool, bytes);
    poolBytes = bytes;
    bUseMutex_ = bUseMutex;
    name_ = InlineString<32>(name);
}

void TlsfAllocator::InitGrowable(size_t baselineBytes, size_t budgetBytes, bool bUseMutex, const char* name)
{
    assert(baselineBytes > tlsf_size() && "baseline too small for TLSF control structure");
    void* mem = malloc(baselineBytes);
    assert(mem != nullptr && "growable TLSF baseline allocation failed");
    tlsf = tlsf_create_with_pool(mem, baselineBytes);
    chunks_[0] = {mem, tlsf_get_pool(tlsf), baselineBytes};
    chunkCount_ = 1;
    poolBytes = baselineBytes;
    budgetBytes_ = budgetBytes;
    bGrowable_ = true;
    bUseMutex_ = bUseMutex;
    name_ = InlineString<32>(name);
}

bool TlsfAllocator::Grow(size_t minBytes)
{
    if (!bGrowable_ || chunkCount_ >= MAX_CHUNKS) { return false; }
    // TLSF's good-fit search rounds requests up by up to size/32 to stay O(1); a chunk sized to the bytes alone can fail the class lookup right after being added.
    const size_t need = minBytes + (minBytes >> 4) + tlsf_pool_overhead() + 4096;
    size_t chunkBytes = need > GROW_CHUNK_BYTES ? need : GROW_CHUNK_BYTES;
    if (budgetBytes_ != 0 && poolBytes + chunkBytes > budgetBytes_) {
        if (poolBytes + need > budgetBytes_) { return false; }
        chunkBytes = need;
    }
    void* mem = malloc(chunkBytes);
    if (mem == nullptr) { return false; }
    void* pool = tlsf_add_pool(tlsf, mem, chunkBytes);
    if (pool == nullptr) {
        free(mem);
        return false;
    }
    chunks_[chunkCount_] = {mem, pool, chunkBytes};
    chunkCount_ += 1;
    poolBytes += chunkBytes;
    return true;
}

void TlsfAllocator::ReleaseEmptyChunks()
{
    if (!bGrowable_ || chunkCount_ <= 1) { return; }
    std::unique_lock lock(mutex_, std::defer_lock);
    if (bUseMutex_) { lock.lock(); }
    for (size_t i = chunkCount_; i-- > 1;) {
        bool bUsed = false;
        tlsf_walk_pool(chunks_[i].pool, UsedBlockWalker, &bUsed);
        if (!bUsed) {
            tlsf_remove_pool(tlsf, chunks_[i].pool);
            poolBytes -= chunks_[i].bytes;
            free(chunks_[i].mem);
            chunkCount_ -= 1;
            chunks_[i] = chunks_[chunkCount_];
        }
    }
}

void TlsfAllocator::Shutdown()
{
    if (!bGrowable_) { return; }
    for (size_t i = 0; i < chunkCount_; ++i) { free(chunks_[i].mem); }
    chunkCount_ = 0;
    poolBytes = 0;
    tlsf = nullptr;
    bGrowable_ = false;
}

void* TlsfAllocator::Alloc(size_t size, AllocTag tag)
{
    if (size == 0) { return nullptr; }
    std::unique_lock lock(mutex_, std::defer_lock);
    if (bUseMutex_) { lock.lock(); }

    void* raw = tlsf_malloc(tlsf, kHeaderSize + size);
    if (raw == nullptr && Grow(kHeaderSize + size)) {
        raw = tlsf_malloc(tlsf, kHeaderSize + size);
    }
    if (raw == nullptr) {
        fprintf(stderr, "TlsfAllocator '%s' OOM: Alloc %zu bytes (tag %s), used %zu / %zu\n", name_.buf, size, AllocTagName(tag), usedBytes_, poolBytes);
        assert(false && "OOM: TLSF pool exhausted");
    }

    auto* header = static_cast<AllocHeader*>(raw);
    header->tag = tag;
    header->size = static_cast<uint32_t>(size);
    header->_pad = 0;

    usedBytes_ += kHeaderSize + size;
    if (usedBytes_ > highWaterBytes_) { highWaterBytes_ = usedBytes_; }
    allocCount_ += 1;

    return header + 1;
}

void* TlsfAllocator::Realloc(void* ptr, size_t newSize, AllocTag tag)
{
    if (!ptr) { return Alloc(newSize, tag); }
    if (newSize == 0) {
        Free(ptr);
        return nullptr;
    }

    std::unique_lock lock(mutex_, std::defer_lock);
    if (bUseMutex_) { lock.lock(); }

    auto* header = static_cast<AllocHeader*>(ptr) - 1;
    const AllocTag savedTag = header->tag;
    const uint32_t oldSize = header->size;

    void* raw = tlsf_realloc(tlsf, header, kHeaderSize + newSize);
    if (raw == nullptr && Grow(kHeaderSize + newSize)) {
        raw = tlsf_realloc(tlsf, header, kHeaderSize + newSize);
    }
    if (raw == nullptr) {
        fprintf(stderr, "TlsfAllocator '%s' OOM: Realloc %zu bytes (tag %s), used %zu / %zu\n", name_.buf, newSize, AllocTagName(savedTag), usedBytes_, poolBytes);
        assert(false && "OOM: TLSF pool exhausted");
    }

    header = static_cast<AllocHeader*>(raw);
    header->tag = savedTag;
    header->size = static_cast<uint32_t>(newSize);

    usedBytes_ -= kHeaderSize + oldSize;
    usedBytes_ += kHeaderSize + newSize;
    if (usedBytes_ > highWaterBytes_) { highWaterBytes_ = usedBytes_; }

    return header + 1;
}

void TlsfAllocator::Free(void* ptr)
{
    if (!ptr) { return; }
    std::unique_lock lock(mutex_, std::defer_lock);
    if (bUseMutex_) { lock.lock(); }

    auto* header = static_cast<AllocHeader*>(ptr) - 1;
    usedBytes_ -= kHeaderSize + header->size;
    allocCount_ -= 1;

    tlsf_free(tlsf, header);
}

void* TlsfAllocator::AlignedAlloc(size_t size, size_t alignment, AllocTag tag)
{
    if (size == 0) { return nullptr; }
    std::unique_lock lock(mutex_, std::defer_lock);
    if (bUseMutex_) { lock.lock(); }

    void* ptr = tlsf_memalign(tlsf, alignment, size);
    if (ptr == nullptr && Grow(size + alignment)) {
        ptr = tlsf_memalign(tlsf, alignment, size);
    }
    if (ptr == nullptr) {
        fprintf(stderr, "TlsfAllocator '%s' OOM: AlignedAlloc %zu bytes align %zu (tag %s), used %zu / %zu\n", name_.buf, size, alignment, AllocTagName(tag), usedBytes_, poolBytes);
        assert(false && "OOM: TLSF pool exhausted");
    }

    usedBytes_ += tlsf_block_size(ptr);
    if (usedBytes_ > highWaterBytes_) { highWaterBytes_ = usedBytes_; }
    allocCount_ += 1;

    return ptr;
}

void TlsfAllocator::AlignedFree(void* ptr)
{
    if (!ptr) { return; }
    std::unique_lock lock(mutex_, std::defer_lock);
    if (bUseMutex_) { lock.lock(); }

    usedBytes_ -= tlsf_block_size(ptr);
    allocCount_ -= 1;

    tlsf_free(tlsf, ptr);
}

TlsfAllocator::Stats TlsfAllocator::GetStats() const
{
    return {poolBytes, usedBytes_, poolBytes - usedBytes_, allocCount_, highWaterBytes_, budgetBytes_};
}

void TlsfAllocator::TagWalker(void* ptr, size_t /*size*/, int used, void* user)
{
    if (!used) { return; }
    auto* ctx = static_cast<TagWalkCtx*>(user);
    auto* header = static_cast<AllocHeader*>(ptr);

    const auto idx = static_cast<size_t>(header->tag);
    if (idx >= static_cast<size_t>(AllocTag::Count)) { return; }

    TagStats& entry = ctx->out[idx];
    entry.tag = header->tag;
    entry.count += 1;
    entry.usedBytes += header->size;
}

void TlsfAllocator::GetTagStats(TagStats out[static_cast<size_t>(AllocTag::Count)])
{
    constexpr auto count = static_cast<size_t>(AllocTag::Count);
    memset(out, 0, sizeof(TagStats) * count);
    for (size_t i = 0; i < count; ++i) {
        out[i].tag = static_cast<AllocTag>(i);
    }

    std::unique_lock lock(mutex_, std::defer_lock);
    if (bUseMutex_) { lock.lock(); }
    TagWalkCtx ctx{out};
    if (bGrowable_) {
        for (size_t i = 0; i < chunkCount_; ++i) {
            tlsf_walk_pool(chunks_[i].pool, TagWalker, &ctx);
        }
    }
    else {
        tlsf_walk_pool(tlsf_get_pool(tlsf), TagWalker, &ctx);
    }
}
} // Core
