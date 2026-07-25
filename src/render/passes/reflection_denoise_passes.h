//
// Created by William on 2026-07-09.
//

#ifndef WILL_ENGINE_REFLECTION_DENOISE_PASSES_H
#define WILL_ENGINE_REFLECTION_DENOISE_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Core { struct ViewFamily; struct ReflectionConfiguration; }

namespace Render
{
class PipelineManager;

void SetupReflectionRELAXDenoiser(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  const RenderTargets& targets,
                                  const Core::RELAXParams& params,
                                  uint64_t frameNumber,
                                  uint32_t activeCheckerboardField,
                                  float checkerboardResolveAccumSpeed,
                                  const Core::ReflectionConfiguration& reflectionConfig);
} // Render

#endif //WILL_ENGINE_REFLECTION_DENOISE_PASSES_H
