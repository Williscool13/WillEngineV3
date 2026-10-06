//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_ANTI_ALIASING_PASSES_H
#define WILL_ENGINE_ANTI_ALIASING_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "core/string_id.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;

RDGTexture SetupSubpixelMorphologicalAntiAliasing(RenderGraph& graph,
                                                  PipelineManager* pipelineManager,
                                                  const Core::ViewFamily& viewFamily,
                                                  Core::Extent2D renderExtent,
                                                  const RenderTargets& targets,
                                                  const SceneResources& scene);

RDGTexture SetupSMAA_T2X(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Extent2D renderExtent,
                         const RenderTargets& targets,
                         const SceneResources& scene);

RDGTexture SetupTemporalAntiAliasing(RenderGraph& graph,
                                     PipelineManager* pipelineManager,
                                     const Core::ViewFamily& viewFamily,
                                     Core::Extent2D renderExtent,
                                     const RenderTargets& targets,
                                     const SceneResources& scene,
                                     StringID pipelineSID);

RDGTexture SetupDonutTemporalAntiAliasing(RenderGraph& graph,
                                          PipelineManager* pipelineManager,
                                          const Core::ViewFamily& viewFamily,
                                          Core::Extent2D inputExtent,
                                          Core::Extent2D outputExtent,
                                          const RenderTargets& targets,
                                          const SceneResources& scene);

/**
 * In-house FSR 2.2.1 (extern/fsr2). Upscales colorOutput from renderExtent to outputExtent and replaces the AA stage.
 * @param reflectionConfig roughness caps for the mirror reactive term (fsr2.reflectionReactive)
 * @param deltaTime seconds, for auto exposure smoothing
 * @param framerateScale fps / 60
 * @param preExposure scale colorOutput was written with this frame
 * @param prevPreExposure scale the history was written with
 */
RDGTexture SetupFsr2(RenderGraph& graph,
                     PipelineManager* pipelineManager,
                     const Core::ViewFamily& viewFamily,
                     Core::Extent2D renderExtent,
                     Core::Extent2D outputExtent,
                     const RenderTargets& targets,
                     const SceneResources& scene,
                     const Core::ReflectionConfiguration& reflectionConfig,
                     float deltaTime,
                     float framerateScale,
                     uint64_t frameNumber,
                     float preExposure,
                     float prevPreExposure);
} // Render

#endif //WILL_ENGINE_ANTI_ALIASING_PASSES_H
