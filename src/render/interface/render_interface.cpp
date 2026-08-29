//
// Created by William on 2025-12-12.
//

#include "render_interface.h"

#include <tracy/Tracy.hpp>

#include "core/math/math_helpers.h"
#include "engine/logging/engine_log.h"

namespace Core
{
ViewFamily::ViewFamily(Arena& arena, const ViewFamilyWatermarks& wm)
{
    portalViews = ArenaFixedVector<PortalView>(&arena, Render::VIEW_COUNT - 1);

    modelMatrices = ArenaVector<Model>(&arena, wm.modelMatrices);
    lightPayload = ArenaVector<LightInfo>(&arena, wm.lightPayload);
    lightRuns = ArenaVector<DirtyRun>(&arena, wm.lightRuns);
    reflectionProbes = ArenaFixedVector<ReflectionProbeGPU>(&arena, MAX_REFLECTION_PROBES);
    emissiveTriWork = ArenaFixedVector<EmissiveTriLightWork>(&arena, MAX_EMISSIVE_GROUPS);
    probePreviews = ArenaFixedVector<ProbePreviewSphere>(&arena, MAX_REFLECTION_PROBES);
    localDDGIVolumes = ArenaFixedVector<LocalDDGIVolume>(&arena, MAX_LOCAL_DDGI_VOLUMES);

    primitiveInstances = ArenaVector<Instance>(&arena, wm.primitiveInstances);
    worldGlyphQuads = ArenaVector<WorldGlyphQuad>(&arena, wm.worldGlyphQuads);
    textInstances = ArenaVector<TextInstanceDataFull>(&arena, wm.textInstances);

    textDrawCalls = ArenaVector<TextDrawCall>(&arena, wm.textDrawCalls);

    activeMaterials = ArenaVector<ActiveMaterial>(&arena, wm.activeMaterials);
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

static ViewFamilyWatermarks ObservedWatermarks(const ViewFamily& vf)
{
    ViewFamilyWatermarks w{};
    w.primitiveInstances = NextPowerOfTwo(vf.primitiveInstances.Size());
    w.worldGlyphQuads = NextPowerOfTwo(vf.worldGlyphQuads.Size());
    w.textInstances = NextPowerOfTwo(vf.textInstances.Size());
    w.modelMatrices = NextPowerOfTwo(vf.modelMatrices.Size());
    w.lightPayload = NextPowerOfTwo(vf.lightPayload.Size());
    w.lightRuns = NextPowerOfTwo(vf.lightRuns.Size());
    w.activeMaterials = vf.activeMaterials.Size();
    w.activeTextMaterials = vf.activeTextMaterials.Size();
    w.textMaterials = vf.textMaterials.Size();
    w.debugLines = vf.debugLines.Size();
    w.debugBoxes = vf.debugBoxes.Size();
    w.debugSpheres = vf.debugSpheres.Size();
    w.debugRects = vf.debugRects.Size();
    w.debugArrows = vf.debugArrows.Size();
    w.debugCapsules = vf.debugCapsules.Size();
    w.debugCylinders = vf.debugCylinders.Size();
    w.uiDrawCommands = vf.uiDrawList.Size();
    w.uiGlyphQuads = vf.uiGlyphQuads.Size();
    w.textDrawCalls = vf.textDrawCalls.Size();
    w.sprites = vf.sprites.Size();
    w.spriteBatches = vf.spriteBatches.Size();
    return w;
}

static ViewFamilyWatermarks MaxWatermarks(const ViewFamilyWatermarks& a, const ViewFamilyWatermarks& b)
{
    ViewFamilyWatermarks w{};
    w.primitiveInstances = std::max(a.primitiveInstances, b.primitiveInstances);
    w.worldGlyphQuads = std::max(a.worldGlyphQuads, b.worldGlyphQuads);
    w.textInstances = std::max(a.textInstances, b.textInstances);
    w.modelMatrices = std::max(a.modelMatrices, b.modelMatrices);
    w.lightPayload = std::max(a.lightPayload, b.lightPayload);
    w.lightRuns = std::max(a.lightRuns, b.lightRuns);
    w.activeMaterials = std::max(a.activeMaterials, b.activeMaterials);
    w.activeTextMaterials = std::max(a.activeTextMaterials, b.activeTextMaterials);
    w.textMaterials = std::max(a.textMaterials, b.textMaterials);
    w.debugLines = std::max(a.debugLines, b.debugLines);
    w.debugBoxes = std::max(a.debugBoxes, b.debugBoxes);
    w.debugSpheres = std::max(a.debugSpheres, b.debugSpheres);
    w.debugRects = std::max(a.debugRects, b.debugRects);
    w.debugArrows = std::max(a.debugArrows, b.debugArrows);
    w.debugCapsules = std::max(a.debugCapsules, b.debugCapsules);
    w.debugCylinders = std::max(a.debugCylinders, b.debugCylinders);
    w.uiDrawCommands = std::max(a.uiDrawCommands, b.uiDrawCommands);
    w.uiGlyphQuads = std::max(a.uiGlyphQuads, b.uiGlyphQuads);
    w.textDrawCalls = std::max(a.textDrawCalls, b.textDrawCalls);
    w.sprites = std::max(a.sprites, b.sprites);
    w.spriteBatches = std::max(a.spriteBatches, b.spriteBatches);
    return w;
}

void FrameBuffer::Reinitialize()
{
    ZoneScoped;
    const Arena::Stats arenaStats = frameArena.Get().GetStats();
    if (!bArenaPeakWarned && arenaStats.peakBytes * 4 > arenaStats.totalBytes * 3) {
        LOG_WARN(Renderer, "Frame arena peak {:.1f} MB crossed 75% of {:.0f} MB reserved", arenaStats.peakBytes / (1024.0f * 1024.0f), arenaStats.totalBytes / (1024.0f * 1024.0f));
        bArenaPeakWarned = true;
    }

    size_t trimTo = 0;
    shrinkWindowPeak = std::max(shrinkWindowPeak, arenaStats.usedBytes);
    windowWatermarks = MaxWatermarks(windowWatermarks, ObservedWatermarks(mainViewFamily));
    if (++shrinkWindowFrames >= FRAME_ARENA_SHRINK_WINDOW) {
        const size_t target = NextPowerOfTwo(shrinkWindowPeak);
        if (arenaStats.committedBytes > target * FRAME_ARENA_SHRINK_RATIO) {
            trimTo = std::max(target, arenaStats.committedBytes / 2);
        }
        maxWatermarks = windowWatermarks;
        windowWatermarks = ViewFamilyWatermarks{};
        shrinkWindowPeak = 0;
        shrinkWindowFrames = 0;
    }
    else {
        maxWatermarks = MaxWatermarks(maxWatermarks, windowWatermarks);
    }

    mainViewFamily = ViewFamily{};
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>{};
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>{};
    frameArena.Get().Reset();
    if (trimTo) {
        frameArena.Get().Trim(trimTo);
    }
    mainViewFamily = ViewFamily(frameArena.Get(), maxWatermarks);
    bufferAcquireOperations = ArenaVector<BufferAcquireOperation>(&frameArena.Get(), 2048);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}
} // Core
