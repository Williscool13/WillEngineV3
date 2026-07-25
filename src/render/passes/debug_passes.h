//
// Created by William on 2026-07-06.
//

#ifndef WILL_ENGINE_DEBUG_PASSES_H
#define WILL_ENGINE_DEBUG_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Core
{
struct ViewFamily;
}

namespace Render
{
class PipelineManager;

/** Ensures the GPU debug draw buffers exist and, when not locked, resets the segment counter. */
void SetupGPUDebugBegin(RenderGraph& graph, bool bLocked);

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

/** Draws a cubemap-shaded preview sphere (roughness-mip or irradiance) at every gathered reflection probe's capture position, into the lit target with depth test. No-op when the request is inactive. */
void SetupProbePreviewSpheres(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, StringID depthTarget, StringID targetImage, const Core::ViewFamily& viewFamily);

/**
 * Emits the froxel cluster grid (FrustumBinning) AABBs as world-space wireframe boxes into the GPU debug segment buffer, colored by depth slice.
 */
void SetupClusterGridDebug(RenderGraph& graph, PipelineManager* pipelineManager, uint32_t sceneIndex, float clusterZNear, float clusterZFar);

/** debugLevel < 0 draws every cascade tinted by level; >= 0 draws only that cascade, untinted. */
void SetupWorldGridDebug(RenderGraph& graph, PipelineManager* pipelineManager, uint32_t sceneIndex, int32_t debugLevel);
} // Render

#endif //WILL_ENGINE_DEBUG_PASSES_H
