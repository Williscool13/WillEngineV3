//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_UI_PASSES_H
#define WILL_ENGINE_UI_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "core/string_id.h"

namespace Render
{
class PipelineManager;

void SetupUIRender(RenderGraph& graph,
                   PipelineManager* pipelineManager,
                   const Core::ViewFamily& viewFamily,
                   Core::Array<uint32_t, 2> renderExtent,
                   StringID targetImage);

void SetupSelectionOutlinePass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               Core::Array<uint32_t, 2> renderExtent,
                               const RenderTargets& targets,
                               uint64_t selectedStableId);
} // Render

#endif //WILL_ENGINE_UI_PASSES_H
