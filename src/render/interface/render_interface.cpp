//
// Created by William on 2025-12-12.
//

#include "render_interface.h"

namespace Core
{
ViewFamily::ViewFamily(Arena& arena, const ViewFamilyWatermarks& wm)
{
    portalViews = ArenaFixedVector<PortalView>(&arena, Render::VIEW_COUNT - 1);

    modelMatrices = ArenaVector<Model>(&arena, wm.modelMatrices);

    primitiveInstances = ArenaVector<PrimitiveInstanceData>(&arena, wm.primitiveInstances);
    worldGlyphQuads = ArenaVector<WorldGlyphQuad>(&arena, wm.worldGlyphQuads);
    textInstances = ArenaVector<TextInstanceDataFull>(&arena, wm.textInstances);

    lightingBuckets = ArenaFixedMap<StringID, uint32_t>(&arena, 256);
    textDrawCalls = ArenaVector<TextDrawCall>(&arena, wm.textDrawCalls);

    activeMaterials = ArenaMap<Engine::MaterialID, uint32_t>(&arena, wm.activeMaterials);
    lightEntityToIndex = ArenaMap<uint32_t, uint32_t>(&arena, MAX_LIGHTS);
    materials = ArenaVector<Engine::RenderMaterial>(&arena, wm.materials);
    activeTextMaterials = ArenaMap<Engine::TextMaterialID, uint32_t>(&arena, wm.activeTextMaterials);
    textMaterials = ArenaVector<TextRenderMaterial>(&arena, wm.textMaterials);

    debugLines = ArenaVector<DebugLine>(&arena, wm.debugLines);
    debugBoxes = ArenaVector<DebugBox>(&arena, wm.debugBoxes);
    debugSpheres = ArenaVector<DebugSphere>(&arena, wm.debugSpheres);
    debugRects = ArenaVector<DebugRect>(&arena, wm.debugRects);
    debugArrows = ArenaVector<DebugArrow>(&arena, wm.debugArrows);
    debugCapsules = ArenaVector<DebugCapsule>(&arena, wm.debugCapsules);
    debugCylinders = ArenaVector<DebugCylinder>(&arena, wm.debugCylinders);

    uiDrawList = ArenaVector<UIDrawCommand>(&arena, wm.uiDrawCommands);
    uiGlyphQuads = ArenaVector<UIGlyphQuad>(&arena, wm.uiGlyphQuads);

    sprites = ArenaVector<Sprite>(&arena, wm.sprites);
    spriteBatches = ArenaVector<SpriteBatch>(&arena, wm.spriteBatches);
}

void FrameBuffer::Initialize(ArenaSuballocator& pool, AllocTag tag)
{
    frameArena = ManagedArena(pool, 16ull * 1024 * 1024, tag);
    mainViewFamily = ViewFamily(frameArena.Get());
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>(&frameArena.Get(), 2048);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}

void FrameBuffer::Reinitialize()
{
    const ViewFamily& vf = mainViewFamily;
    viewFamilyWatermarks.primitiveInstances = std::max(viewFamilyWatermarks.primitiveInstances, vf.primitiveInstances.Size());
    viewFamilyWatermarks.worldGlyphQuads = std::max(viewFamilyWatermarks.worldGlyphQuads, vf.worldGlyphQuads.Size());
    viewFamilyWatermarks.textInstances = std::max(viewFamilyWatermarks.textInstances, vf.textInstances.Size());
    viewFamilyWatermarks.modelMatrices = std::max(viewFamilyWatermarks.modelMatrices, vf.modelMatrices.Size());
    viewFamilyWatermarks.activeMaterials = std::max(viewFamilyWatermarks.activeMaterials, vf.activeMaterials.Size());
    viewFamilyWatermarks.materials = std::max(viewFamilyWatermarks.materials, vf.materials.Size());
    viewFamilyWatermarks.activeTextMaterials = std::max(viewFamilyWatermarks.activeTextMaterials, vf.activeTextMaterials.Size());
    viewFamilyWatermarks.textMaterials = std::max(viewFamilyWatermarks.textMaterials, vf.textMaterials.Size());
    viewFamilyWatermarks.debugLines = std::max(viewFamilyWatermarks.debugLines, vf.debugLines.Size());
    viewFamilyWatermarks.debugBoxes = std::max(viewFamilyWatermarks.debugBoxes, vf.debugBoxes.Size());
    viewFamilyWatermarks.debugSpheres = std::max(viewFamilyWatermarks.debugSpheres, vf.debugSpheres.Size());
    viewFamilyWatermarks.debugRects = std::max(viewFamilyWatermarks.debugRects, vf.debugRects.Size());
    viewFamilyWatermarks.debugArrows = std::max(viewFamilyWatermarks.debugArrows, vf.debugArrows.Size());
    viewFamilyWatermarks.debugCapsules = std::max(viewFamilyWatermarks.debugCapsules, vf.debugCapsules.Size());
    viewFamilyWatermarks.debugCylinders = std::max(viewFamilyWatermarks.debugCylinders, vf.debugCylinders.Size());
    viewFamilyWatermarks.uiDrawCommands = std::max(viewFamilyWatermarks.uiDrawCommands, vf.uiDrawList.Size());
    viewFamilyWatermarks.uiGlyphQuads = std::max(viewFamilyWatermarks.uiGlyphQuads, vf.uiGlyphQuads.Size());
    viewFamilyWatermarks.textDrawCalls = std::max(viewFamilyWatermarks.textDrawCalls, vf.textDrawCalls.Size());
    viewFamilyWatermarks.sprites = std::max(viewFamilyWatermarks.sprites, vf.sprites.Size());
    viewFamilyWatermarks.spriteBatches = std::max(viewFamilyWatermarks.spriteBatches, vf.spriteBatches.Size());

    mainViewFamily = ViewFamily{};
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>{};
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>{};
    frameArena.Get().Reset();
    mainViewFamily = ViewFamily(frameArena.Get(), viewFamilyWatermarks);
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>(&frameArena.Get(), 2048);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}
} // Core
