//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_UI_PASSES_H
#define WILL_ENGINE_UI_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "core/string_id.h"
#include "core/types/extent.h"

namespace Render
{
class PipelineManager;

void SetupUIRender(RenderGraph& graph,
                   PipelineManager* pipelineManager,
                   const Core::ViewFamily& viewFamily,
                   Core::Extent2D renderExtent,
                   const SceneResources& scene,
                   RDGTexture targetImage);

void SetupSelectionOutlinePass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               Core::Extent2D renderExtent,
                               const RenderTargets& targets,
                               uint64_t selectedStableId);
} // Render

#endif //WILL_ENGINE_UI_PASSES_H
