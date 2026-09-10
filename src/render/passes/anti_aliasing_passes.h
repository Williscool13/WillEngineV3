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

StringID SetupDonutTemporalAntiAliasing(RenderGraph& graph,
                                        PipelineManager* pipelineManager,
                                        const Core::ViewFamily& viewFamily,
                                        Core::Array<uint32_t, 2> inputExtent,
                                        Core::Array<uint32_t, 2> outputExtent,
                                        const RenderTargets& targets);

/**
 * In-house FSR 2.2.1 (extern/fsr2). Upscales colorOutput from renderExtent to outputExtent and replaces the AA stage.
 * @param bHasPreOverlayColor "lit_color_preoverlay" was snapshotted this frame; enables the reactive mask
 * @param deltaTime seconds, for auto exposure smoothing
 */
StringID SetupFsr2(RenderGraph& graph,
                   PipelineManager* pipelineManager,
                   const Core::ViewFamily& viewFamily,
                   Core::Array<uint32_t, 2> renderExtent,
                   Core::Array<uint32_t, 2> outputExtent,
                   const RenderTargets& targets,
                   bool bHasPreOverlayColor,
                   float deltaTime,
                   uint64_t frameNumber);
} // Render

#endif //WILL_ENGINE_ANTI_ALIASING_PASSES_H
