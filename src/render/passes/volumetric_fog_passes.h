//
// Created by William on 2026-10-03.
//

#ifndef WILL_ENGINE_VOLUMETRIC_FOG_PASSES_H
#define WILL_ENGINE_VOLUMETRIC_FOG_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;

inline const StringID VOLUMETRIC_FOG_SCATTER = "fog_scatter"_sid;
inline const StringID VOLUMETRIC_FOG_INTEGRATED = "fog_integrated"_sid;
/** colorOutput right after fog, before overlays; stands in for lit_color_preoverlay wherever FSR2 and metering compare against the final image. */
inline const StringID LIT_COLOR_FOGGED = "lit_color_fogged"_sid;

/**
 * Froxel fog over targets.colorOutput, after the Lit Color Snapshot so next frame's screen-space GI and reflections stay unfogged. No-op without an enabled fog.
 * Lit by sky ambient, the sun and one RIS-picked world grid light per froxel, each with a shadow ray when the TLAS exists.
 * Each froxel samples a jittered point and blends into its reprojected history; the history drops on camera cuts and resizes.
 * @param bFoggedCopy also write LIT_COLOR_FOGGED (declared by the caller)
 */
void SetupVolumetricFog(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Array<uint32_t, 2> renderExtent,
                        const RenderTargets& targets,
                        uint32_t sceneIndex,
                        uint64_t frameIndex,
                        bool bFoggedCopy);
} // Render

#endif //WILL_ENGINE_VOLUMETRIC_FOG_PASSES_H
