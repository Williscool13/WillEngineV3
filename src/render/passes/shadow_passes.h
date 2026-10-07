//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_SHADOW_PASSES_H
#define WILL_ENGINE_SHADOW_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;

/** SetupSigmaShadowDenoise intermediates read by SetupSigmaShadowTemporal. */
struct SigmaDenoiseFrame
{
    RDGTexture blurred;
    RDGTexture tilesSmoothed;
};

/**
 * Culls every instance against every cascade in one chain and draws all cascades into one depth atlas.
 * @return the atlas, invalid without instances
 */
RDGTexture SetupCSMDepth(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         const SceneBufferSizes& bufferSizes,
                         const SceneResources& scene,
                         RDGBuffer csmData,
                         uint32_t cascadeCount,
                         uint32_t sceneIndex);

/** Samples the CSM atlas into the sun visibility the directional lighting pass reads. */
SunShadowFrame SetupCSMResolve(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               Core::Extent2D renderExtent,
                               const RenderTargets& targets,
                               const SceneResources& scene,
                               RDGBuffer csmData,
                               RDGTexture atlas,
                               uint32_t sceneIndex);

/**
 * Writes targets.shadows, temporally filtering GTAO when it ran.
 * @param gtao invalid when GTAO did not run
 */
void SetupShadowsResolve(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Extent2D renderExtent,
                         const RenderTargets& targets,
                         const SceneResources& scene,
                         const GTAOFrame& gtao,
                         uint32_t sceneIndex);

/**
 * SIGMA shadow denoiser (penumbra-aware spatial filter) over the rt_sun_shadow signal.
 * Writes denoised (visibility, penumbra) to sigma_shadow. No-ops if rt_sun_shadow is absent.
 * @param sunShadow sigmaShadow is filled in
 */
SigmaDenoiseFrame SetupSigmaShadowDenoise(RenderGraph& graph,
                                          PipelineManager* pipelineManager,
                                          const Core::ViewFamily& viewFamily,
                                          Core::Extent2D renderExtent,
                                          const RenderTargets& targets,
                                          const SceneResources& scene,
                                          SunShadowFrame& sunShadow,
                                          uint32_t sceneIndex,
                                          uint64_t frameNumber);

/**
 * @brief SIGMA temporal stabilization: motion-vector reproject + neighborhood-clamp the previous
 * sigma_shadow result. Writes sigma_stabilized and carries it to next frame. No-ops if sigma_shadow is absent.
 * @param denoise SetupSigmaShadowDenoise's return
 * @param sunShadow sigmaStabilized is filled in
 */
void SetupSigmaShadowTemporal(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Extent2D renderExtent,
                              const RenderTargets& targets,
                              const SceneResources& scene,
                              const SigmaDenoiseFrame& denoise,
                              SunShadowFrame& sunShadow,
                              uint32_t sceneIndex);
} // Render

#endif //WILL_ENGINE_SHADOW_PASSES_H
