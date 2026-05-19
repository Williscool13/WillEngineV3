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

    instances = ArenaVector<InstanceData>(&arena, wm.instances);
    worldGlyphQuads = ArenaVector<WorldGlyphQuad>(&arena, wm.worldGlyphQuads);
    textInstances = ArenaVector<TextInstanceDataFull>(&arena, wm.textInstances);

    lightingBuckets = ArenaFixedMap<StringID, uint32_t>(&arena, 256);
    textDrawCalls = ArenaVector<TextDrawCall>(&arena, wm.textDrawCalls);

    activeMaterials = ArenaMap<Engine::MaterialID, uint32_t>(&arena, wm.activeMaterials);
    materials = ArenaVector<Engine::RenderMaterial>(&arena, wm.materials);
    activeTextMaterials = ArenaMap<Engine::TextMaterialID, uint32_t>(&arena, wm.activeTextMaterials);
    textMaterials = ArenaVector<TextRenderMaterial>(&arena, wm.textMaterials);

    debugLines = ArenaVector<DebugLine>(&arena, wm.debugLines);
    debugBoxes = ArenaVector<DebugBox>(&arena, wm.debugBoxes);
    debugSpheres = ArenaVector<DebugSphere>(&arena, wm.debugSpheres);

    uiRects = ArenaVector<UIRectData>(&arena, wm.uiRects);
    uiImageCommands = ArenaVector<UIRenderCommandImage>(&arena, wm.uiImageCommands);
    uiGlyphQuads = ArenaVector<UIGlyphQuad>(&arena, wm.uiGlyphQuads);
    uiTextDrawCalls = ArenaVector<UITextDrawCall>(&arena, wm.uiTextDrawCalls);
}

FrameBuffer::FrameBuffer(ArenaSuballocator& pool, Core::AllocTag tag)
    : frameArena(pool, 4ull * 1024 * 1024, tag)
{
    mainViewFamily = ViewFamily(frameArena.Get());
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>(&frameArena.Get(), 2048);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}

void FrameBuffer::Reinitialize()
{
    const ViewFamily& vf = mainViewFamily;
    viewFamilyWatermarks.instances = std::max(viewFamilyWatermarks.instances, vf.instances.Size());
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
    viewFamilyWatermarks.uiRects = std::max(viewFamilyWatermarks.uiRects, vf.uiRects.Size());
    viewFamilyWatermarks.uiImageCommands = std::max(viewFamilyWatermarks.uiImageCommands, vf.uiImageCommands.Size());
    viewFamilyWatermarks.uiGlyphQuads = std::max(viewFamilyWatermarks.uiGlyphQuads, vf.uiGlyphQuads.Size());
    viewFamilyWatermarks.uiTextDrawCalls = std::max(viewFamilyWatermarks.uiTextDrawCalls, vf.uiTextDrawCalls.Size());
    viewFamilyWatermarks.textDrawCalls = std::max(viewFamilyWatermarks.textDrawCalls, vf.textDrawCalls.Size());

    mainViewFamily = ViewFamily{};
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>{};
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>{};
    frameArena.Get().Reset();
    mainViewFamily = ViewFamily(frameArena.Get(), viewFamilyWatermarks);
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>(&frameArena.Get(), 2048);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}
} // Core
