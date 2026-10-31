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
    renderables.modelPayload = ArenaVector<Model>(&arena, wm.modelPayload);
    renderables.modelRuns = ArenaVector<DirtyRun>(&arena, wm.modelRuns);
    lights.lightPayload = ArenaVector<LightInfo>(&arena, wm.lightPayload);
    lights.lightRuns = ArenaVector<DirtyRun>(&arena, wm.lightRuns);
    probes.reflectionProbes = ArenaFixedVector<ReflectionProbeGPU>(&arena, MAX_REFLECTION_PROBES);
    lights.emissiveTriWork = ArenaFixedVector<EmissiveTriLightWork>(&arena, MAX_EMISSIVE_MESHES);
    lights.boundsChanges = ArenaFixedVector<glm::vec4>(&arena, MAX_BOUNDS_CHANGES);
    probes.probePreviews = ArenaFixedVector<ProbePreviewSphere>(&arena, MAX_REFLECTION_PROBES);
    ddgiVolumes.localDDGIVolumes = ArenaFixedVector<LocalDDGIVolume>(&arena, MAX_LOCAL_DDGI_VOLUMES);

    renderables.instancePayload = ArenaVector<Instance>(&arena, wm.instancePayload);
    renderables.instanceRuns = ArenaVector<DirtyRun>(&arena, wm.instanceRuns);
    text.worldGlyphQuads = ArenaVector<WorldGlyphQuad>(&arena, wm.worldGlyphQuads);
    text.textInstances = ArenaVector<TextInstanceDataFull>(&arena, wm.textInstances);

    text.textDrawCalls = ArenaVector<TextDrawCall>(&arena, wm.textDrawCalls);

    renderables.activeMaterials = ArenaVector<ActiveMaterial>(&arena, wm.activeMaterials);
    renderables.materialPayload = ArenaVector<MaterialProperties>(&arena, wm.materialPayload);
    renderables.materialRuns = ArenaVector<DirtyRun>(&arena, wm.materialRuns);
    text.activeTextMaterials = ArenaMap<Engine::TextMaterialID, uint32_t>(&arena, wm.activeTextMaterials);
    text.textMaterials = ArenaVector<TextRenderMaterial>(&arena, wm.textMaterials);

    debugDraws.debugLines = ArenaVector<DebugLine>(&arena, wm.debugLines);
    debugDraws.debugBoxes = ArenaVector<DebugBox>(&arena, wm.debugBoxes);
    debugDraws.debugSpheres = ArenaVector<DebugSphere>(&arena, wm.debugSpheres);
    debugDraws.debugRects = ArenaVector<DebugRect>(&arena, wm.debugRects);
    debugDraws.debugArrows = ArenaVector<DebugArrow>(&arena, wm.debugArrows);
    debugDraws.debugCapsules = ArenaVector<DebugCapsule>(&arena, wm.debugCapsules);
    debugDraws.debugCylinders = ArenaVector<DebugCylinder>(&arena, wm.debugCylinders);

    ui.uiDrawList = ArenaVector<UIDrawCommand>(&arena, wm.uiDrawCommands);
    ui.uiGlyphQuads = ArenaVector<UIGlyphQuad>(&arena, wm.uiGlyphQuads);

    spriteDraws.sprites = ArenaVector<Sprite>(&arena, wm.sprites);
    spriteDraws.spriteBatches = ArenaVector<SpriteBatch>(&arena, wm.spriteBatches);
}

void FrameBuffer::Initialize(VirtualMemoryManager& vm, AllocTag tag, const char* name)
{
    frameArena = VirtualArena(vm, FRAME_ARENA_RESERVE_BYTES, tag, name);
    mainViewFamily = ViewFamily(frameArena.Get());
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}

static ViewFamilyWatermarks ObservedWatermarks(const ViewFamily& vf)
{
    ViewFamilyWatermarks w{};
    w.instancePayload = NextPowerOfTwo(vf.renderables.instancePayload.Size());
    w.instanceRuns = NextPowerOfTwo(vf.renderables.instanceRuns.Size());
    w.worldGlyphQuads = NextPowerOfTwo(vf.text.worldGlyphQuads.Size());
    w.textInstances = NextPowerOfTwo(vf.text.textInstances.Size());
    w.modelPayload = NextPowerOfTwo(vf.renderables.modelPayload.Size());
    w.modelRuns = NextPowerOfTwo(vf.renderables.modelRuns.Size());
    w.lightPayload = NextPowerOfTwo(vf.lights.lightPayload.Size());
    w.lightRuns = NextPowerOfTwo(vf.lights.lightRuns.Size());
    w.materialPayload = NextPowerOfTwo(vf.renderables.materialPayload.Size());
    w.materialRuns = NextPowerOfTwo(vf.renderables.materialRuns.Size());
    w.activeMaterials = vf.renderables.activeMaterials.Size();
    w.activeTextMaterials = vf.text.activeTextMaterials.Size();
    w.textMaterials = vf.text.textMaterials.Size();
    w.debugLines = vf.debugDraws.debugLines.Size();
    w.debugBoxes = vf.debugDraws.debugBoxes.Size();
    w.debugSpheres = vf.debugDraws.debugSpheres.Size();
    w.debugRects = vf.debugDraws.debugRects.Size();
    w.debugArrows = vf.debugDraws.debugArrows.Size();
    w.debugCapsules = vf.debugDraws.debugCapsules.Size();
    w.debugCylinders = vf.debugDraws.debugCylinders.Size();
    w.uiDrawCommands = vf.ui.uiDrawList.Size();
    w.uiGlyphQuads = vf.ui.uiGlyphQuads.Size();
    w.textDrawCalls = vf.text.textDrawCalls.Size();
    w.sprites = vf.spriteDraws.sprites.Size();
    w.spriteBatches = vf.spriteDraws.spriteBatches.Size();
    return w;
}

static ViewFamilyWatermarks MaxWatermarks(const ViewFamilyWatermarks& a, const ViewFamilyWatermarks& b)
{
    ViewFamilyWatermarks w{};
    w.instancePayload = std::max(a.instancePayload, b.instancePayload);
    w.instanceRuns = std::max(a.instanceRuns, b.instanceRuns);
    w.worldGlyphQuads = std::max(a.worldGlyphQuads, b.worldGlyphQuads);
    w.textInstances = std::max(a.textInstances, b.textInstances);
    w.modelPayload = std::max(a.modelPayload, b.modelPayload);
    w.modelRuns = std::max(a.modelRuns, b.modelRuns);
    w.lightPayload = std::max(a.lightPayload, b.lightPayload);
    w.lightRuns = std::max(a.lightRuns, b.lightRuns);
    w.materialPayload = std::max(a.materialPayload, b.materialPayload);
    w.materialRuns = std::max(a.materialRuns, b.materialRuns);
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
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>{};
    frameArena.Get().Reset();
    if (trimTo) {
        frameArena.Get().Trim(trimTo);
    }
    mainViewFamily = ViewFamily(frameArena.Get(), maxWatermarks);
    imageAcquireOperations = ArenaVector<ImageAcquireOperation>(&frameArena.Get(), 2048);
}
} // Core
