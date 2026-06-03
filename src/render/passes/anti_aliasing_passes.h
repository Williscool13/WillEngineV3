//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_ANTI_ALIASING_PASSES_H
#define WILL_ENGINE_ANTI_ALIASING_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "core/string_id.h"

namespace Render
{
class PipelineManager;

StringID SetupSubpixelMorphologicalAntiAliasing(RenderGraph& graph,
                                                PipelineManager* pipelineManager,
                                                const Core::ViewFamily& viewFamily,
                                                Core::Array<uint32_t, 2> renderExtent,
                                                const MainRenderTargets& ppTargets);

StringID SetupSMAA_T2X(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const MainRenderTargets& ppTargets);

StringID SetupTemporalAntiAliasing(RenderGraph& graph,
                                   PipelineManager* pipelineManager,
                                   const Core::ViewFamily& viewFamily,
                                   Core::Array<uint32_t, 2> renderExtent,
                                   const MainRenderTargets& ppTargets,
                                   StringID pipelineSID);
} // Render

#endif //WILL_ENGINE_ANTI_ALIASING_PASSES_H
