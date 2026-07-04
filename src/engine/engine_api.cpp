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

void EditorTextureResidency::Tick(EngineContext* ctx)
{
    for (auto it = pendingRemoval.begin(); it != pendingRemoval.end();) {
        if (ctx->currentRenderFrame >= it->freeOnFrame) {
            ctx->removeImguiTextureFn(it->descSet);
            ctx->assetManager->UnloadTexture(it->texture->textureId);
            it = pendingRemoval.Remove(it);
        }
        else {
            ++it;
        }
    }
}

void EditorTextureResidency::Acquire(TextureID id, EngineContext* ctx)
{
    if (entries.Contains(id)) { return; }

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

uint64_t EditorTextureResidency::GetDescSet(TextureID id, EngineContext* ctx)
{
    if (sampler->loadState != Sampler::LoadState::Loaded) { return 0; }

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

void EditorTextureResidency::Release(TextureID id, EngineContext* ctx)
{
    auto it = entries.Find(id);
    if (!it) { return; }
    Entry& e = *it;
    e.freeOnFrame = ctx->currentRenderFrame + Core::FRAME_BUFFER_COUNT * 2;
    pendingRemoval.PushBack(e);
    entries.Remove(id);
}

void EditorTextureResidency::ReleaseAll(EngineContext* ctx)
{
    for (const auto& [id, e] : entries) {
        e.freeOnFrame = ctx->currentRenderFrame + Core::FRAME_BUFFER_COUNT * 2;
        pendingRemoval.PushBack(e);
    }
    entries.Clear();
}

ComponentRegistry::ComponentRegistry(Core::TlsfAllocator* allocator)
{
    registry = Core::Vector<ComponentEntry>(allocator, Core::AllocTag::EngineState, 1024);
    registryMapping = Core::Map<StringID, size_t>(allocator, Core::AllocTag::EngineState, 1024);
}

PhysicsState::PhysicsState(Core::TlsfAllocator* allocator)
    : bodyToEntity(allocator, Core::AllocTag::EngineState, 64)
{}

EditorState::EditorState(Core::TlsfAllocator* allocator)
    : selectedEntities(allocator, Core::AllocTag::EngineState, 64),
      prevSelectedEntities(allocator, Core::AllocTag::EngineState, 64),
      selectedFolders(allocator, Core::AllocTag::EngineState, 64),
      texResidency(allocator)
{}

InputState::InputState(Core::TlsfAllocator* allocator)
    : bindings(allocator, Core::AllocTag::EngineState, 128),
      defaultBindings(allocator, Core::AllocTag::EngineState, 128),
      actionIndex(allocator, Core::AllocTag::EngineState, 128),
      actionStates(allocator, Core::AllocTag::EngineState, 128)
{}

EngineState::EngineState(Core::TlsfAllocator* allocator)
    : stableIdToEntityMap(allocator, Core::AllocTag::EngineState, 64),
      componentRegistry(allocator),
      physics(allocator),
      editor(allocator),
      input(allocator)
{
    meshPrimitiveStore.Init(MAX_MESH_PRIMITIVE_INSTANCES, allocator, Core::AllocTag::RenderMesh);

    const uint64_t clayMemorySize = Clay_MinMemorySize();
    void* clayMemory = allocator->Alloc(clayMemorySize, Core::AllocTag::Clay);
    clayArena = Clay_CreateArenaWithCapacityAndMemory(clayMemorySize, clayMemory);
    Clay_Initialize(clayArena, {0.0f, 0.0f}, {nullptr, 0});
}
} // namespace Engine
