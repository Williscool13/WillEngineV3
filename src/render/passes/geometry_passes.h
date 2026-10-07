//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_GEOMETRY_PASSES_H
#define WILL_ENGINE_GEOMETRY_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/interface/render_params.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;

/** SetupVisibilityBucketingPass's tile lists consumed by the shading and bucket debug passes. tileBits is valid only in a bucket debug mode. */
struct VisibilityBucketTiles
{
    RDGBuffer shadingTileList;
    RDGBuffer tileBits;
};

struct MeshletCullBuffers
{
    RDGBuffer instanceMeshletOffsets;
    RDGBuffer level1Sums;
    RDGBuffer level1BlockSums;
    RDGBuffer level2Sums;
    RDGBuffer level2BlockSums;
    RDGBuffer scannedLevel2BlockSums;
    RDGBuffer intermediateMeshlets;
    RDGBuffer meshletLevel1Sums;
    RDGBuffer meshletLevel1BlockSums;
    RDGBuffer meshletLevel2Sums;
    RDGBuffer meshletLevel2BlockSums;
    RDGBuffer meshletScannedLevel2BlockSums;
    RDGBuffer visibleMeshlets;
    RDGBuffer meshletCountDispatchArgs;
    RDGBuffer compactedMeshletDispatchArgs;
};

MeshletCullBuffers CreateMeshletCullBuffers(RenderGraph& graph, const MeshletCullBufferSizes& sizes, const char* namePrefix);

void AddMeshletCullClear(RenderGraph& graph, const MeshletCullBuffers& buffers, const char* passPrefix, RenderCategory category);

void AddInstanceMeshletPrefixSum(RenderGraph& graph, PipelineManager* pipelineManager, const MeshletCullBuffers& buffers, uint32_t elementCount, const char* passPrefix, RenderCategory category);

void AddMeshletCompaction(RenderGraph& graph, PipelineManager* pipelineManager, const MeshletCullBuffers& buffers, uint32_t meshletUpperBound, RDGBuffer readback, size_t meshletCountOffset,
                          bool bRegionStats, const char* passPrefix, RenderCategory category);

/**
 * Two-phase meshlet cull and visibility buffer draw.
 * @return the Hi-Z pyramid, invalid when phase 2 did not run
 */
RDGTexture SetupGeometryPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             const SceneBufferSizes& bufferSizes,
                             const Core::DebugRenderParams& debug,
                             Core::Extent2D renderExtent,
                             const RenderTargets& targets,
                             const SceneResources& scene,
                             uint32_t sceneIndex);

/**
 * Per-tile shade and lighting bucketing.
 * @param outTiles filled for the shading and bucket debug passes
 * @return invalid when the scene has no bucketing buffers
 */
GeometryFrame SetupVisibilityBucketingPass(RenderGraph& graph,
                                           PipelineManager* pipelineManager,
                                           const Core::ViewFamily& viewFamily,
                                           Core::Extent2D renderExtent,
                                           const RenderTargets& targets,
                                           const SceneResources& scene,
                                           uint32_t sceneIndex,
                                           Core::BucketDebugMode bucketDebugMode,
                                           VisibilityBucketTiles& outTiles);

void SetupVisibilityShadingPass(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                const Core::ViewFamily& viewFamily,
                                Core::Extent2D renderExtent,
                                const RenderTargets& targets,
                                const SceneResources& scene,
                                const VisibilityBucketTiles& tiles,
                                uint32_t sceneIndex,
                                Core::Arena& arena);

/**
 * Bucket debug views written to bucket_debug_target for the debug visualizer, one group per tile off the bounds pass's per-tile bucket bitset.
 * Buckets modes: fill = the pixel's own bucket, concentric ring n = the n-th bucket dispatched to the tile; Heat modes: tile color by bucket count.
 * @param tiles SetupVisibilityBucketingPass's output
 */
void SetupBucketDebugPass(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Extent2D renderExtent,
                          const RenderTargets& targets,
                          const SceneResources& scene,
                          const VisibilityBucketTiles& tiles,
                          Core::BucketDebugMode bucketDebugMode);
} // Render

#endif //WILL_ENGINE_GEOMETRY_PASSES_H
