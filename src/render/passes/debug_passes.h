//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_DEBUG_PASSES_H
#define WILL_ENGINE_DEBUG_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;

/**
 * Ensures the GPU debug draw buffers exist and, when not locked, resets the segment counter.
 * @param graph
 * @param pipelineManager
 * @param bLocked
 * @param bTestPattern spawns the reference producer pass (animated grid of boxes)
 * @param frameNumber
 */
void SetupGPUDebugBegin(RenderGraph& graph, PipelineManager* pipelineManager, bool bLocked, bool bTestPattern, uint64_t frameNumber);

/**
 * Converts the appended counts into indirect args and draws the segments and spheres (spheres first, opaque depth-writing; blended lines on top).
 * Carries all debug buffers to the next frame so a lock keeps rendering the last written set.
 * @param graph
 * @param pipelineManager
 * @param renderExtent
 * @param depthTarget
 * @param targetImage
 * @param bLocked
 */
void SetupGPUDebugDraw(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, StringID depthTarget, StringID targetImage, bool bLocked);
} // Render

#endif //WILL_ENGINE_DEBUG_PASSES_H
