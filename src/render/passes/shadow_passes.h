//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_SHADOW_PASSES_H
#define WILL_ENGINE_SHADOW_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;

void SetupShadowsResolve(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Array<uint32_t, 2> renderExtent,
                         const RenderTargets& targets,
                         uint32_t sceneIndex);

/**
 * SIGMA shadow denoiser (penumbra-aware spatial filter) over the rt_sun_shadow signal.
 * Writes denoised (visibility, penumbra) to sigma_shadow. No-ops if rt_sun_shadow is absent.
 */
void SetupSigmaShadowDenoise(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Array<uint32_t, 2> renderExtent,
                             const RenderTargets& targets,
                             uint32_t sceneIndex,
                             uint64_t frameNumber);

/**
 * @brief SIGMA temporal stabilization: motion-vector reproject + neighborhood-clamp the previous
 * sigma_shadow result. Writes sigma_stabilized and carries it to next frame. No-ops if sigma_shadow is absent.
 */
void SetupSigmaShadowTemporal(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex);
} // Render

#endif //WILL_ENGINE_SHADOW_PASSES_H
