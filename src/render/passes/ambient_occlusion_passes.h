//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_AMBIENT_OCCLUSION_PASSES_H
#define WILL_ENGINE_AMBIENT_OCCLUSION_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;

GTAOFrame SetupGroundTruthAmbientOcclusion(RenderGraph& graph,
                                           PipelineManager* pipelineManager,
                                           const Core::ViewFamily& viewFamily,
                                           Core::Extent2D renderExtent,
                                           const SceneResources& scene,
                                           const RenderTargets& targets,
                                           uint64_t frameNumber,
                                           uint32_t sceneIndex);
} // Render

#endif //WILL_ENGINE_AMBIENT_OCCLUSION_PASSES_H
