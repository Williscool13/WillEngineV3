//
// Created by William on 2026-08-08.
//

#ifndef WILL_ENGINE_OCCLUSION_PASSES_H
#define WILL_ENGINE_OCCLUSION_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;

inline const StringID HIZ_PYRAMID = SID("hiz_pyramid");
inline const StringID HIZ_DEBUG_TARGET = SID("hiz_debug_target");

/**
 * Min-reduce depth pyramid from the depth attachment (call after the phase-1 draw). Mip 0 is pow2-down of half render extent.
 */
void SetupHiZPyramid(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets);

/**
 * Writes the chosen pyramid mip nearest-upscaled to hiz_debug_target for the debug visualizer.
 */
void SetupHiZDebug(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, int32_t mip);
} // Render

#endif //WILL_ENGINE_OCCLUSION_PASSES_H
