//
// Created by William on 2026-10-03.
//

#ifndef WILL_ENGINE_VOLUMETRIC_FOG_PASSES_H
#define WILL_ENGINE_VOLUMETRIC_FOG_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;

inline const StringID VOLUMETRIC_FOG_TILE_DEPTH = "fog_tile_depth"_sid;
inline const StringID VOLUMETRIC_FOG_SCATTER = "fog_scatter"_sid;
inline const StringID VOLUMETRIC_FOG_FILTERED = "fog_filtered"_sid;
inline const StringID VOLUMETRIC_FOG_INTEGRATED = "fog_integrated"_sid;
inline const StringID VOLUMETRIC_FOG_DEBUG_TARGET = "fog_debug_target"_sid;

/**
 * Froxel fog over targets.colorOutput, after the Lit Color Snapshot so next frame's screen-space GI and reflections stay unfogged. No-op without an enabled fog.
 * Lit by DDGI (sky ambient outside its coverage), the sun and one RIS-picked world grid light per froxel, each with a shadow ray when the TLAS exists.
 * Each froxel samples a jittered point and blends into its reprojected history; the history drops on camera cuts and resizes.
 * Froxels behind their tile's farthest surface skip lighting and carry their history forward.
 * @param scene sceneData, lightData and the TLAS (shadow rays when valid)
 * @param worldGrid light grid for the per-froxel RIS light; skipped when invalid
 * @param ddgi sampled for ambient when bDDGIApply and its cascades exist
 * @param bPreOverlayCopy also write targets.preOverlayColor (declared by the caller)
 * @param bDDGIApply sample DDGI for ambient when its cascades exist
 * @param debugMode DebugRenderParams::fogDebugMode; nonzero isolates a term and writes VOLUMETRIC_FOG_DEBUG_TARGET
 * @param bResetHistory ignore last frame's scatter, e.g. when the debug mode changed
 */
void SetupVolumetricFog(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Extent2D renderExtent,
                        const RenderTargets& targets,
                        const SceneResources& scene,
                        const WorldGridFrame& worldGrid,
                        const DDGIFrame& ddgi,
                        uint32_t sceneIndex,
                        uint64_t frameIndex,
                        bool bPreOverlayCopy,
                        bool bDDGIApply,
                        int32_t debugMode,
                        bool bResetHistory);
} // Render

#endif //WILL_ENGINE_VOLUMETRIC_FOG_PASSES_H
