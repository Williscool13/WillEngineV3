//
// Created by William on 2025-12-14.
//

#include "engine_api.h"

#include "asset_manager.h"
#include "engine/include/engine_context.h"
#include "resources/texture/texture.h"

namespace Engine
{
EditorTextureResidency::EditorTextureResidency(Core::TlsfAllocator* allocator)
    : entries(allocator, Core::AllocTag::EngineContext, 64),
      pendingRemoval(allocator, Core::AllocTag::EngineContext, 64)
{}

void EditorTextureResidency::Tick(Engine::EngineContext* ctx)
{
    for (auto it = pendingRemoval.begin(); it != pendingRemoval.end();) {
        if (ctx->currentFrame >= it->freeOnFrame) {
            ctx->removeImguiTextureFn(it->descSet);
            ctx->assetManager->UnloadTexture(it->texture->textureId);
            it = pendingRemoval.Remove(it);
        }
        else {
            ++it;
        }
    }
}

void EditorTextureResidency::Acquire(TextureID id, Engine::EngineContext* ctx)
{
    if (entries.Contains(id)) return;

    // Recover from destruction queue if re-acquired before cleanup
    for (auto it = pendingRemoval.begin(); it != pendingRemoval.end(); ++it) {
        if (it->texture && it->texture->textureId == id) {
            entries[id] = *it;
            pendingRemoval.Remove(it);
            return;
        }
    }

    if (!sampler) {
        SamplerDesc desc{};
        desc.minFilter = VK_FILTER_NEAREST;
        desc.magFilter = VK_FILTER_NEAREST;
        sampler = ctx->assetManager->LoadSampler(desc);
    }
    entries[id].texture = ctx->assetManager->LoadTexture(id);
}

uint64_t EditorTextureResidency::GetDescSet(TextureID id, Engine::EngineContext* ctx)
{
    auto it = entries.Find(id);
    if (!it) { return 0; }
    Entry& e = *it;
    if (e.texture && e.texture->loadState == Texture::LoadState::Loaded && !e.descSet) {
        e.descSet = ctx->addImguiTextureFn(
            reinterpret_cast<uint64_t>(sampler->sampler.handle),
            reinterpret_cast<uint64_t>(e.texture->imageView.handle));
    }
    return e.descSet;
}

void EditorTextureResidency::Release(TextureID id, Engine::EngineContext* ctx)
{
    auto it = entries.Find(id);
    if (!it) { return; }
    Entry& e = *it;
    e.freeOnFrame = ctx->currentFrame + Core::FRAME_BUFFER_COUNT + 1;
    pendingRemoval.PushBack(e);
    entries.Remove(id);
}

void EditorTextureResidency::ReleaseAll(Engine::EngineContext* ctx)
{
    for (const auto& [id, e] : entries) {
        e.freeOnFrame = ctx->currentFrame + Core::FRAME_BUFFER_COUNT + 1;
        pendingRemoval.PushBack(e);
    }
    entries.Clear();
}

ComponentRegistry::ComponentRegistry(Core::TlsfAllocator* allocator)
{
    registry = Core::Vector<ComponentEntry>(allocator, Core::AllocTag::EngineState, 1024);
    registryMapping = Core::Map<StringID, size_t>(allocator, Core::AllocTag::EngineState, 1024);
}

EngineState::EngineState(Core::TlsfAllocator* allocator)
    : stableIdToEntityMap(allocator, Core::AllocTag::EngineState, 64),
      componentRegistry(allocator),
      bodyToEntity(allocator, Core::AllocTag::EngineState, 64),
      selectedEntities(allocator, Core::AllocTag::EngineState, 64),
      prevSelectedEntities(allocator, Core::AllocTag::EngineState, 64),
      texResidency(allocator)
{}
} // namespace Engine
