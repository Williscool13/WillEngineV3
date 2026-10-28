//
// Created by William on 2026-10-27.
//

#include "deferred_commands.h"

#include <algorithm>

#include "core/memory/tlsf_allocator.h"
#include "engine/core/registry.h"

namespace Engine
{
DeferredCommands::~DeferredCommands()
{
    for (const Chunk& chunk : chunks) {
        allocator->AlignedFree(chunk.data);
    }
}

void DeferredCommands::Init(Core::TlsfAllocator* allocator_)
{
    allocator = allocator_;
    chunks = Core::Vector<Chunk>(allocator, Core::AllocTag::ECS);
}

void* DeferredCommands::Push(ApplyFn fn, entt::entity entity, size_t payloadSize, [[maybe_unused]] StringID setType)
{
    const uint32_t stride = static_cast<uint32_t>(sizeof(RecordHeader) + ((payloadSize + RECORD_ALIGN - 1) & ~(RECORD_ALIGN - 1)));
    while (true) {
        if (current == chunks.Size()) {
            const uint32_t capacity = std::max(CHUNK_BYTES, stride);
            chunks.PushBack({.data = static_cast<std::byte*>(allocator->AlignedAlloc(capacity, RECORD_ALIGN, Core::AllocTag::ECS)), .capacity = capacity});
        }
        Chunk& chunk = chunks[current];
        if (chunk.used + stride <= chunk.capacity) { break; }
        if (chunk.used == 0) {
            allocator->AlignedFree(chunk.data);
            chunk.capacity = stride;
            chunk.data = static_cast<std::byte*>(allocator->AlignedAlloc(stride, RECORD_ALIGN, Core::AllocTag::ECS));
            break;
        }
        ++current;
    }

    Chunk& chunk = chunks[current];
    auto* header = new(chunk.data + chunk.used) RecordHeader{fn, entity, stride};
#if WILL_ACCESS_CHECK
    header->setType = setType;
#endif
    chunk.used += stride;
    return header + 1;
}

void DeferredCommands::ApplyChanges(Registry& registry)
{
    for (uint32_t c = 0; c < chunks.Size() && c <= current; ++c) {
        Chunk& chunk = chunks[c];
        for (uint32_t offset = 0; offset < chunk.used;) {
            auto* header = reinterpret_cast<RecordHeader*>(chunk.data + offset);
            if (header->apply != nullptr) { header->apply(registry, header->entity, header + 1); }
            offset += header->stride;
        }
    }
}

void DeferredCommands::ApplyDestroys(Registry& registry)
{
    for (uint32_t c = 0; c < chunks.Size() && c <= current; ++c) {
        Chunk& chunk = chunks[c];
        for (uint32_t offset = 0; offset < chunk.used;) {
            auto* header = reinterpret_cast<RecordHeader*>(chunk.data + offset);
            if (header->apply == nullptr && registry.Valid(header->entity)) { registry.Destroy(header->entity); }
            offset += header->stride;
        }
        chunk.used = 0;
    }
    current = 0;
}

void ApplyDeferredCommands(Registry& registry, DeferredCommands* const* buffers, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) { buffers[i]->ApplyChanges(registry); }
    for (uint32_t i = 0; i < count; ++i) { buffers[i]->ApplyDestroys(registry); }
}
} // Engine
