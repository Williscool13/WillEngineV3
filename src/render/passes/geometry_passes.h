//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_GEOMETRY_PASSES_H
#define WILL_ENGINE_GEOMETRY_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;

void SetupGeometryPass(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       const RenderFamilyProperties& renderFamilyProperties,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex);

void SetupVisibilityBarycentricDerivativePass(RenderGraph& graph,
                                              PipelineManager* pipelineManager,
                                              const Core::ViewFamily& viewFamily,
                                              Core::Array<uint32_t, 2> renderExtent,
                                              const RenderTargets& targets,
                                              uint32_t sceneIndex);

void SetupVisibilityBucketingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  const RenderTargets& targets,
                                  uint32_t sceneIndex);

void SetupVisibilityShadingPass(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                const Core::ViewFamily& viewFamily,
                                Core::Array<uint32_t, 2> renderExtent,
                                const RenderTargets& targets,
                                uint32_t sceneIndex,
                                Core::Arena& arena);

void SetupVisibilityBucketingDebugPass(RenderGraph& graph,
                                       PipelineManager* pipelineManager,
                                       const Core::ViewFamily& viewFamily,
                                       Core::Array<uint32_t, 2> renderExtent,
                                       const RenderTargets& targets,
                                       uint32_t sceneIndex,
                                       Core::Arena& arena);

void SetupLightingBucketingDebugPass(RenderGraph& graph,
                                     PipelineManager* pipelineManager,
                                     const Core::ViewFamily& viewFamily,
                                     Core::Array<uint32_t, 2> renderExtent,
                                     const RenderTargets& targets,
                                     uint32_t sceneIndex);
} // Render

#endif //WILL_ENGINE_GEOMETRY_PASSES_H
