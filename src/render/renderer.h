//
// Created by William on 2026-04-16.
//

#ifndef WILL_ENGINE_RENDERER_H
#define WILL_ENGINE_RENDERER_H
#include "renderer_types.h"
#include "render-graph/render_graph.h"
#include "types/render_types.h"
#include "post-processing/post_processing.h"

namespace Render
{
class PipelineManager;


void SetupGeometryPass(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       const RenderFamilyProperties& renderFamilyProperties,
                       Core::Array<uint32_t, 2> renderExtent,
                       const VisibilityBufferTargets& targets,
                       uint32_t sceneIndex);


void SetupVisibilityBarycentricDerivativePass(RenderGraph& graph,
                                              PipelineManager* pipelineManager,
                                              const Core::ViewFamily& viewFamily,
                                              Core::Array<uint32_t, 2> renderExtent,
                                              const VisibilityBufferBarycentricDerivativeTargets& targets,
                                              uint32_t sceneIndex);

void SetupVisibilityBucketingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  const VisibilityBufferBarycentricDerivativeTargets& targets,
                                  uint32_t sceneIndex);


void SetupVisibilityShadingPass(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                const Core::ViewFamily& viewFamily,
                                Core::Array<uint32_t, 2> renderExtent,
                                const VisibilityShadingTargets& targets,
                                uint32_t sceneIndex,
                                Core::Arena& arena);

void SetupVisibilityBucketingDebugPass(RenderGraph& graph,
                                       PipelineManager* pipelineManager,
                                       const Core::ViewFamily& viewFamily,
                                       Core::Array<uint32_t, 2> renderExtent,
                                       const VisibilityShadingTargets& targets,
                                       uint32_t sceneIndex,
                                       Core::Arena& arena);

void SetupLightingBucketingDebugPass(RenderGraph& graph,
                                     PipelineManager* pipelineManager,
                                     const Core::ViewFamily& viewFamily,
                                     Core::Array<uint32_t, 2> renderExtent,
                                     const VisibilityShadingTargets& targets,
                                     uint32_t sceneIndex);


void SetupVisibilityLightingResolvePass(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> renderExtent,
                                        const DeferredResolveTargets& targets,
                                        uint32_t sceneIndex,
                                        Core::Arena& arena);


void SetupGroundTruthAmbientOcclusion(RenderGraph& graph,
                                      PipelineManager* pipelineManager,
                                      const Core::ViewFamily& viewFamily,
                                      Core::Array<uint32_t, 2> renderExtent,
                                      const MainRenderTargets& targets,
                                      uint64_t frameNumber,
                                      uint32_t sceneIndex);


void SetupShadowsResolve(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Array<uint32_t, 2> renderExtent,
                         const MainRenderTargets& targets,
                         uint32_t sceneIndex);

void SetupSkyboxRendering(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Array<uint32_t, 2> renderExtent,
                          const MainRenderTargets& targets,
                          uint32_t sceneIndex);

/**
 * Prepares the render graph pass for text rendering in a forward-shading blending pass.
 * Note: Run before TAA. Rationale is text is almost never "free-floating" so the surface right behind the text will emit consistent motion vectors.
 * @param graph
 * @param pipelineManager
 * @param viewFamily
 * @param renderExtent
 * @param targets
 */
void SetupTextForwardPass(RenderGraph& graph,
                          PipelineManager* pipelineManager,
                          const Core::ViewFamily& viewFamily,
                          Core::Array<uint32_t, 2> renderExtent,
                          const MainRenderTargets& targets);

StringID SetupSubpixelMorphologicalAntiAliasing(RenderGraph& graph,
                                                PipelineManager* pipelineManager,
                                                const Core::ViewFamily& viewFamily,
                                                Core::Array<uint32_t, 2> renderExtent,
                                                const MainRenderTargets& ppTargets);

StringID SetupTemporalAntiAliasing(RenderGraph& graph,
                                   PipelineManager* pipelineManager,
                                   const Core::ViewFamily& viewFamily,
                                   Core::Array<uint32_t, 2> renderExtent,
                                   const MainRenderTargets& ppTargets);

StringID SetupSMAA_T2X(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const MainRenderTargets& ppTargets);

StringID SetupPostProcessing(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Array<uint32_t, 2> renderExtent,
                             const MainRenderTargets& targets,
                             float deltaTime,
                             uint64_t frameNumber);
} // Render

#endif //WILL_ENGINE_RENDERER_H
