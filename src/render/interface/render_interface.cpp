//
// Created by William on 2025-12-12.
//

#include "render_interface.h"

namespace Core
{
ViewFamily::ViewFamily(Arena& arena)
{
    portalViews = ArenaFixedVector<PortalView>(&arena, Render::VIEW_COUNT - 1);

    modelMatrices = ArenaVector<Model>(&arena, 256);

    instances = ArenaVector<InstanceData>(&arena, 128);
    worldGlyphQuads = ArenaVector<WorldGlyphQuad>(&arena, 256);
    textInstances = ArenaVector<TextInstanceDataFull>(&arena, 32);

    lightingBuckets = ArenaFixedMap<StringID, uint32_t>(&arena, 256);
    textDrawCalls = ArenaVector<TextDrawCall>(&arena, 256);

    activeMaterials = ArenaFixedMap<Engine::MaterialID, uint32_t>(&arena, 256);
    materials = ArenaVector<Engine::RenderMaterial>(&arena, 256);
    activeTextMaterials = ArenaFixedMap<Engine::TextMaterialID, uint32_t>(&arena, 256);
    textMaterials = ArenaVector<TextRenderMaterial>(&arena, 256);

    debugLines = ArenaVector<DebugLine>(&arena, 256);
    debugBoxes = ArenaVector<DebugBox>(&arena, 256);
    debugSpheres = ArenaVector<DebugSphere>(&arena, 256);
}

FrameBuffer::FrameBuffer(ArenaSuballocator& pool)
    : frameArena(pool, 4ull * 1024 * 1024, AllocTag::FrameSync)
{
    mainViewFamily = ViewFamily(frameArena.Get());
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>(&frameArena.Get(), 2048);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}

void FrameBuffer::Reinitialize()
{
    mainViewFamily = ViewFamily{};
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>{};
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>{};
    frameArena.Get().Reset();
    mainViewFamily = ViewFamily(frameArena.Get());
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>(&frameArena.Get(), 2048);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}
} // Core
