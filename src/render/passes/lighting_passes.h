//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_LIGHTING_PASSES_H
#define WILL_ENGINE_LIGHTING_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;

void SetupVisibilityLightingResolvePass(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> renderExtent,
                                        const RenderTargets& targets,
                                        uint32_t sceneIndex,
                                        Core::Arena& arena,
                                        uint64_t frameNumber);

void SetupGroundTruthLightingPass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  const RenderTargets& targets,
                                  uint32_t sceneIndex,
                                  bool bReset,
                                  uint32_t accumulationCount,
                                  uint64_t frameNumber);
} // Render

#endif //WILL_ENGINE_LIGHTING_PASSES_H
