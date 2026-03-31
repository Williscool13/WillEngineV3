//
// Created by William on 2025-12-14.
//

#include "engine_api.h"

#include "asset_manager.h"
#include "engine/include/engine_context.h"
#include "resources/texture/texture.h"

namespace Engine
{
void EditorTextureResidency::Tick(Core::EngineContext* ctx)
{
    for (auto it = pendingRemoval.begin(); it != pendingRemoval.end();) {
        if (ctx->currentFrame >= it->second) {
            ctx->removeImguiTextureFn(it->first.descSet);
            ctx->assetManager->UnloadTexture(it->first.texture->textureId);
            it = pendingRemoval.erase(it);
        }
        else {
            ++it;
        }
    }
}

void EditorTextureResidency::Acquire(TextureID id, Core::EngineContext* ctx)
{
    if (entries.contains(id)) return;

    // Recover from destruction queue if re-acquired before cleanup
    for (auto it = pendingRemoval.begin(); it != pendingRemoval.end(); ++it) {
        if (it->first.texture && it->first.texture->textureId == id) {
            entries[id] = it->first;
            pendingRemoval.erase(it);
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

uint64_t EditorTextureResidency::GetDescSet(TextureID id, Core::EngineContext* ctx)
{
    auto it = entries.find(id);
    if (it == entries.end()) return 0;
    Entry& e = it->second;
    if (e.texture && e.texture->loadState == Texture::LoadState::Loaded && !e.descSet) {
        e.descSet = ctx->addImguiTextureFn(
            reinterpret_cast<uint64_t>(sampler->sampler.handle),
            reinterpret_cast<uint64_t>(e.texture->imageView.handle));
    }
    return e.descSet;
}

void EditorTextureResidency::Release(TextureID id, Core::EngineContext* ctx)
{
    auto it = entries.find(id);
    if (it == entries.end()) return;
    Entry& e = it->second;
    pendingRemoval.push_back({e, ctx->currentFrame + Core::FRAME_BUFFER_COUNT + 1});
    entries.erase(it);
}

void EditorTextureResidency::ReleaseAll(Core::EngineContext* ctx)
{
    for (auto& [id, e] : entries) {
        pendingRemoval.push_back({e, ctx->currentFrame + Core::FRAME_BUFFER_COUNT + 1});
    }
    entries.clear();
}
} // namespace Engine
