//
// Created by William on 2026-08-01.
//

#ifndef WILL_ENGINE_EDITOR_TEXTURE_RESIDENCY_H
#define WILL_ENGINE_EDITOR_TEXTURE_RESIDENCY_H

#include <cstdint>

#include "core/containers/map.h"
#include "core/containers/vector.h"

namespace Engine
{
struct EngineContext;
struct TextureID;
struct Texture;
struct Sampler;

/** Deferred-release residency for editor-previewed textures; ImGui samples them for frames after the last use. */
struct EditorTextureResidency
{
    struct Entry
    {
        Texture* texture{nullptr};
        uint64_t descSet{0};
        uint64_t freeOnFrame{~0x0ULL};
    };

    Sampler* sampler{nullptr};
    Core::Map<TextureID, Entry> entries{};
    Core::Vector<Entry> pendingRemoval{};

    EditorTextureResidency() = default;
    explicit EditorTextureResidency(Core::TlsfAllocator* allocator);
    ~EditorTextureResidency() = default;

    void Tick(EngineContext* ctx);
    void Acquire(TextureID id, EngineContext* ctx);
    uint64_t GetDescSet(TextureID id, EngineContext* ctx);
    void Release(TextureID id, EngineContext* ctx);
    void ReleaseAll(EngineContext* ctx);
};
} // Engine

#endif //WILL_ENGINE_EDITOR_TEXTURE_RESIDENCY_H
