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
                                                const RenderTargets& targets);

StringID SetupSMAA_T2X(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets);

StringID SetupTemporalAntiAliasing(RenderGraph& graph,
                                   PipelineManager* pipelineManager,
                                   const Core::ViewFamily& viewFamily,
                                   Core::Array<uint32_t, 2> renderExtent,
                                   const RenderTargets& targets,
                                   StringID pipelineSID);

// Donut-ported native-res TAA resolve (shaders/donut_taa.slang).
StringID SetupDonutTemporalAntiAliasing(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> renderExtent,
                                        const RenderTargets& targets);
} // Render

#endif //WILL_ENGINE_ANTI_ALIASING_PASSES_H
