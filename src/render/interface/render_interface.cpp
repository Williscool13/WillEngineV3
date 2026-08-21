//
// Created by William on 2025-12-12.
//

#include "render_interface.h"

#include "core/math/math_helpers.h"
#include "engine/logging/engine_log.h"

namespace Core
{
ViewFamily::ViewFamily(Arena& arena, const ViewFamilyWatermarks& wm)
{
    portalViews = ArenaFixedVector<PortalView>(&arena, Render::VIEW_COUNT - 1);

    modelMatrices = ArenaVector<Model>(&arena, wm.modelMatrices);
    lights = ArenaVector<LightInfo>(&arena, wm.lights);
    reflectionProbes = ArenaFixedVector<ReflectionProbeGPU>(&arena, MAX_REFLECTION_PROBES);
    emissiveGroups = ArenaFixedVector<EmissiveGroup>(&arena, MAX_EMISSIVE_GROUPS);
    probePreviews = ArenaFixedVector<ProbePreviewSphere>(&arena, MAX_REFLECTION_PROBES);
    localDDGIVolumes = ArenaFixedVector<LocalDDGIVolume>(&arena, MAX_LOCAL_DDGI_VOLUMES);

    primitiveInstances = ArenaVector<PrimitiveInstanceData>(&arena, wm.primitiveInstances);
    worldGlyphQuads = ArenaVector<WorldGlyphQuad>(&arena, wm.worldGlyphQuads);
    textInstances = ArenaVector<TextInstanceDataFull>(&arena, wm.textInstances);

    lightingBuckets = ArenaFixedMap<StringID, uint32_t>(&arena, 256);
    textDrawCalls = ArenaVector<TextDrawCall>(&arena, wm.textDrawCalls);

    activeMaterials = ArenaVector<ActiveMaterial>(&arena, wm.activeMaterials);
    triLightBaseBySlot = ArenaVector<uint32_t>(&arena);
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

void FrameBuffer::Initialize(VirtualMemoryManager& vm, AllocTag tag, const char* name)
{
    frameArena = VirtualArena(vm, FRAME_ARENA_RESERVE_BYTES, tag, name);
    mainViewFamily = ViewFamily(frameArena.Get());
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>(&frameArena.Get(), 2048);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}

void FrameBuffer::Reinitialize()
{
    const Arena::Stats arenaStats = frameArena.Get().GetStats();
    if (!bArenaPeakWarned && arenaStats.peakBytes * 4 > arenaStats.totalBytes * 3) {
        LOG_WARN(Renderer, "Frame arena peak {:.1f} MB crossed 75% of {:.0f} MB reserved", arenaStats.peakBytes / (1024.0f * 1024.0f), arenaStats.totalBytes / (1024.0f * 1024.0f));
        bArenaPeakWarned = true;
    }

    size_t trimTo = 0;
    shrinkWindowPeak = std::max(shrinkWindowPeak, arenaStats.usedBytes);
    if (++shrinkWindowFrames >= FRAME_ARENA_SHRINK_WINDOW) {
        const size_t target = std::max(NextPowerOfTwo(shrinkWindowPeak), VirtualMemoryManager::COMMIT_STEP);
        if (arenaStats.committedBytes > target * FRAME_ARENA_SHRINK_RATIO) {
            trimTo = std::max(target, arenaStats.committedBytes / 2);
        }
        shrinkWindowPeak = 0;
        shrinkWindowFrames = 0;
    }
    if (trimTo) {
        viewFamilyWatermarks = ViewFamilyWatermarks{};
    }

    const ViewFamily& vf = mainViewFamily;
    viewFamilyWatermarks.primitiveInstances = std::max(viewFamilyWatermarks.primitiveInstances, NextPowerOfTwo(vf.primitiveInstances.Size()));
    viewFamilyWatermarks.worldGlyphQuads = std::max(viewFamilyWatermarks.worldGlyphQuads, NextPowerOfTwo(vf.worldGlyphQuads.Size()));
    viewFamilyWatermarks.textInstances = std::max(viewFamilyWatermarks.textInstances, NextPowerOfTwo(vf.textInstances.Size()));
    viewFamilyWatermarks.modelMatrices = std::max(viewFamilyWatermarks.modelMatrices, NextPowerOfTwo(vf.modelMatrices.Size()));
    viewFamilyWatermarks.lights = std::max(viewFamilyWatermarks.lights, vf.lights.Size());
    viewFamilyWatermarks.activeMaterials = std::max(viewFamilyWatermarks.activeMaterials, vf.activeMaterials.Size());
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
    if (trimTo) {
        frameArena.Get().Trim(trimTo);
    }
    mainViewFamily = ViewFamily(frameArena.Get(), viewFamilyWatermarks);
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>(&frameArena.Get(), 2048);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}
} // Core
