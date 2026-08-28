//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_LIGHTING_PASSES_H
#define WILL_ENGINE_LIGHTING_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/shaders/ddgi_interop.h"

namespace Render
{
class PipelineManager;
struct DDGICascades;

/** Unused */
void SetupFrustumBinningPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             uint32_t sceneIndex,
                             float clusterZNear,
                             float clusterZFar);

/**
 * Camera-centered cascaded world-space grid, rebuilt every frame; see world_grid_interop.h for the cascade layout.
 * Bins analytic lights, emissive groups, reflection probes and DDGI world volumes.
 * @param ddgiCascades this frame's cascade set; its resident world volumes are binned so DDGISampleIrradianceCascaded can visit a cell's overlaps instead of every slot
 */
void SetupWorldGridBinningPass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               const Core::ViewFamily& viewFamily,
                               uint32_t sceneIndex,
                               Core::Arena& arena,
                               const DDGICascades& ddgiCascades);

void SetupVisibilityLightingResolvePass(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> renderExtent,
                                        const RenderTargets& targets,
                                        uint32_t sceneIndex,
                                        uint64_t frameNumber,
                                        bool bDDGIApply,
                                        uint32_t giGatherMode,
                                        const Core::ReflectionConfiguration& reflectionConfig);

void SetupGroundTruthLightingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  const RenderTargets& targets,
                                  uint32_t sceneIndex,
                                  bool bReset,
                                  uint32_t accumulationCount,
                                  uint64_t frameNumber);

/**
 * Adds the analytic directional (sun) light, modulated by the rt_sun_shadow visibility, into the color target.
 * Runs full-res over the gbuffer; no-ops if the rt_sun_shadow target is absent.
 */
void SetupDirectionalLightingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  Core::Array<uint32_t, 2> shadowExtent,
                                  const RenderTargets& targets,
                                  uint32_t sceneIndex,
                                  uint32_t pixelScale);
} // Render

#endif //WILL_ENGINE_LIGHTING_PASSES_H
