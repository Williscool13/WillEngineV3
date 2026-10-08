//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_LIGHTING_PASSES_H
#define WILL_ENGINE_LIGHTING_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/shaders/ddgi_interop.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;
struct DDGICascades;

/** Unused */
void SetupFrustumBinningPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             const SceneResources& scene,
                             uint32_t sceneIndex,
                             float clusterZNear,
                             float clusterZFar);

/** Must run before anything that reads LightData's triangle region or its meshlets. */
void SetupEmissiveTriLightPass(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, const SceneResources& scene, float emissiveTriRangeMultiplier);

/**
 * Camera-centered cascaded world-space grid, rebuilt every frame; see world_grid_interop.h for the cascade layout.
 * @param ddgiCascades this frame's cascade set; its resident world volumes are binned so DDGISampleIrradianceCascaded can visit a cell's overlaps instead of every slot
 * @return invalid when there is no light data
 */
WorldGridFrame SetupWorldGridBinningPass(RenderGraph& graph,
                                         PipelineManager* pipelineManager,
                                         const Core::ViewFamily& viewFamily,
                                         const SceneResources& scene,
                                         uint32_t sceneIndex,
                                         Core::Arena& arena,
                                         const DDGICascades& ddgiCascades);

/** cursorPixel is in render-extent coordinates. */
void SetupDebugWorldGridCursorCellPass(RenderGraph& graph,
                                       PipelineManager* pipelineManager,
                                       const SceneResources& scene,
                                       const WorldGridFrame& worldGrid,
                                       uint32_t sceneIndex,
                                       RDGTexture depthTexture,
                                       Core::Extent2D renderExtent,
                                       Core::Array<uint32_t, 2> cursorPixel);

/** cursorPixel is in render-extent coordinates. */
void SetupDebugReGIRCursorCellPass(RenderGraph& graph,
                                   PipelineManager* pipelineManager,
                                   const SceneResources& scene,
                                   const ReSTIRFrame& restir,
                                   uint32_t sceneIndex,
                                   RDGTexture depthTexture,
                                   Core::Extent2D renderExtent,
                                   Core::Array<uint32_t, 2> cursorPixel);

void SetupDebugPickPixelPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const SceneResources& scene,
                             uint32_t sceneIndex,
                             RDGTexture visibilityTexture,
                             RDGTexture depthTexture,
                             Core::Extent2D renderExtent,
                             Core::Array<uint32_t, 2> pickPixel,
                             uint32_t requestId);

void SetupVisibilityLightingResolvePass(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Extent2D renderExtent,
                                        const RenderTargets& targets,
                                        const SceneResources& scene,
                                        const GeometryFrame& geometry,
                                        const WorldGridFrame& worldGrid,
                                        const DDGIFrame& ddgi,
                                        const FinalGatherFrame& gather,
                                        const ReflectionFrame& reflection,
                                        const ReSTIRFrame& restir,
                                        const LocalShadowFrame& localShadows,
                                        uint32_t sceneIndex,
                                        uint64_t frameNumber,
                                        bool bDDGIApply,
                                        uint32_t giGatherMode,
                                        const Core::ReflectionConfiguration& reflectionConfig);

void SetupGroundTruthLightingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Extent2D renderExtent,
                                  const RenderTargets& targets,
                                  const SceneResources& scene,
                                  uint32_t sceneIndex,
                                  bool bReset,
                                  uint32_t& accumulationCount,
                                  uint64_t frameNumber);
} // Render

#endif //WILL_ENGINE_LIGHTING_PASSES_H
