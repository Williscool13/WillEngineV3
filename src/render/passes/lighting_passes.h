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

/** Unused */
void SetupFrustumBinningPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             uint32_t sceneIndex,
                             float clusterZNear,
                             float clusterZFar);

/** Camera-centered cascaded world-space grid, rebuilt every frame; see world_grid_interop.h for the cascade layout. */
void SetupWorldGridBinningPass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               const Core::ViewFamily& viewFamily,
                               uint32_t sceneIndex);

void SetupVisibilityLightingResolvePass(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> renderExtent,
                                        const RenderTargets& targets,
                                        uint32_t sceneIndex,
                                        Core::Arena& arena,
                                        uint64_t frameNumber,
                                        bool bDDGIApply,
                                        uint32_t giGatherMode,
                                        const Core::RTReflectionConfiguration& reflectionConfig);

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
