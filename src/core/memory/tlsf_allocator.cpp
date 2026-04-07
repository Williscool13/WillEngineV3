//
// Created by William on 2026-03-31.
//

#include "tlsf_allocator.h"

#include <cassert>
#include <cstring>

#include "tlsf.h"

namespace Core
{
const char* AllocTagName(AllocTag tag)
{
    switch (tag) {
        case AllocTag::Unknown: return "Unknown";
        case AllocTag::AssetModel: return "AssetModel";
        case AllocTag::AssetTexture: return "AssetTexture";
        case AllocTag::AssetGenerator: return "AssetGenerator";
        case AllocTag::PhysicsBody: return "PhysicsBody";
        case AllocTag::PhysicsShape: return "PhysicsShape";
        case AllocTag::RenderMesh: return "RenderMesh";
        case AllocTag::RenderMaterial: return "RenderMaterial";
        case AllocTag::Render: return "Render";
        case AllocTag::ECS: return "ECS";
        case AllocTag::TaskScheduler: return "TaskScheduler";
        case AllocTag::SDL: return "SDL";
        case AllocTag::ImGui: return "ImGui";
        case AllocTag::EngineLogger: return "EngineLogger";
        case AllocTag::EngineContext: return "EngineContext";
        case AllocTag::InputManager: return "InputManager";
        case AllocTag::TimeManager: return "TimeManager";
        case AllocTag::FrameSync: return "FrameSync";
        case AllocTag::RenderThread: return "RenderThread";
        case AllocTag::AudioManager: return "AudioManager";
        case AllocTag::AsyncAssetLoadManager: return "AsyncAssetLoadManager";
        case AllocTag::AssetManager: return "AssetManager";
        case AllocTag::Count: return "Count";
    }
    return "Unknown";
}

void TlsfAllocator::Init(void* pool, size_t bytes, bool bUseMutex)
{
    assert(pool != nullptr);
    assert(bytes > tlsf_size() && "pool too small for TLSF control structure");
    tlsf = tlsf_create_with_pool(pool, bytes);
    poolBytes = bytes;
    bUseMutex_ = bUseMutex;
}

void* TlsfAllocator::Alloc(size_t size, AllocTag tag)
{
    if (size == 0) { return nullptr; }
    std::unique_lock lock(mutex_, std::defer_lock);
    if (bUseMutex_) { lock.lock(); }

    void* raw = tlsf_malloc(static_cast<tlsf_t>(tlsf), kHeaderSize + size);
    assert(raw != nullptr && "OOM: TLSF pool exhausted");

    auto* header = static_cast<AllocHeader*>(raw);
    header->tag = tag;
    header->size = static_cast<uint32_t>(size);
    header->_pad = 0;

    usedBytes_ += kHeaderSize + size;
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

    void* raw = tlsf_realloc(static_cast<tlsf_t>(tlsf), header, kHeaderSize + newSize);
    assert(raw != nullptr && "OOM: TLSF pool exhausted");

    header = static_cast<AllocHeader*>(raw);
    header->tag = savedTag;
    header->size = static_cast<uint32_t>(newSize);

    usedBytes_ -= kHeaderSize + oldSize;
    usedBytes_ += kHeaderSize + newSize;

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

    tlsf_free(static_cast<tlsf_t>(tlsf), header);
}

TlsfAllocator::Stats TlsfAllocator::GetStats()
{
    std::unique_lock lock(mutex_, std::defer_lock);
    if (bUseMutex_) { lock.lock(); }
    return {poolBytes, usedBytes_, poolBytes - usedBytes_, allocCount_};
}

void TlsfAllocator::TagWalker(void* ptr, size_t /*size*/, int used, void* user)
{
    if (!used) { return; }
    auto* ctx = static_cast<TagWalkCtx*>(user);
    auto* header = static_cast<AllocHeader*>(ptr);

    const auto idx = static_cast<size_t>(header->tag);
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
    tlsf_walk_pool(tlsf_get_pool(static_cast<tlsf_t>(tlsf)), TagWalker, &ctx);
}
} // Core
