//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_GEOMETRY_PASSES_H
#define WILL_ENGINE_GEOMETRY_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/interface/render_params.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;

void SetupGeometryPass(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       const SceneBufferSizes& bufferSizes,
                       const Core::DebugRenderParams& debug,
                       Core::Extent2D renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex);

void SetupVisibilityBucketingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Extent2D renderExtent,
                                  const RenderTargets& targets,
                                  uint32_t sceneIndex,
                                  Core::BucketDebugMode bucketDebugMode);

void SetupVisibilityShadingPass(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                const Core::ViewFamily& viewFamily,
                                Core::Extent2D renderExtent,
                                const RenderTargets& targets,
                                uint32_t sceneIndex,
                                Core::Arena& arena);

/**
 * Bucket debug views written to bucket_debug_target for the debug visualizer, one group per tile off the bounds pass's per-tile bucket bitset.
 * Buckets modes: fill = the pixel's own bucket, concentric ring n = the n-th bucket dispatched to the tile; Heat modes: tile color by bucket count.
 */
void SetupBucketDebugPass(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Extent2D renderExtent,
                          const RenderTargets& targets,
                          Core::BucketDebugMode bucketDebugMode);
} // Render

#endif //WILL_ENGINE_GEOMETRY_PASSES_H
