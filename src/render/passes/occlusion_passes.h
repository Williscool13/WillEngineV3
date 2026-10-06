//
// Created by William on 2026-08-08.
//

#ifndef WILL_ENGINE_OCCLUSION_PASSES_H
#define WILL_ENGINE_OCCLUSION_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;

inline const StringID HIZ_PYRAMID = "hiz_pyramid"_sid;
inline const StringID HIZ_DEBUG_TARGET = "hiz_debug_target"_sid;

/**
 * Min-reduce depth pyramid from the depth attachment (call after the phase-1 draw). Mip 0 is pow2-down of half render extent.
 * @return the pyramid
 */
RDGTexture SetupHiZPyramid(RenderGraph& graph, PipelineManager* pipelineManager, Core::Extent2D renderExtent, const RenderTargets& targets);

/**
 * Writes the chosen pyramid mip nearest-upscaled to hiz_debug_target for the debug visualizer.
 * @param hizPyramid SetupGeometryPass's return; nothing is recorded when invalid
 */
void SetupHiZDebug(RenderGraph& graph, PipelineManager* pipelineManager, Core::Extent2D renderExtent, RDGTexture hizPyramid, int32_t mip);
} // Render

#endif //WILL_ENGINE_OCCLUSION_PASSES_H
