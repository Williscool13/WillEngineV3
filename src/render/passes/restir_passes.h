//
// Created by William on 2026-06-06.
//

#ifndef WILL_ENGINE_RESTIR_PASSES_H
#define WILL_ENGINE_RESTIR_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/shaders/ddgi_interop.h"

namespace Core
{
struct ViewFamily;
struct Arena;
struct RTReflectionConfiguration;
}

namespace Render
{
class PipelineManager;

void SetupReSTIRPasses(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       const Core::ViewFamily& viewFamily,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex,
                       Core::Arena& arena,
                       uint64_t frameNumber,
                       const Core::ReSTIRParams& restirParams,
                       uint32_t activeCheckerboardField,
                       const Core::RTReflectionConfiguration& reflectionConfig);

void SetupReSTIRLightingResolvePass(RenderGraph& graph,
                                    PipelineManager* pipelineManager,
                                    const Core::ViewFamily& viewFamily,
                                    Core::Array<uint32_t, 2> renderExtent,
                                    const RenderTargets& targets,
                                    uint32_t sceneIndex,
                                    Core::Arena& arena,
                                    uint64_t frameNumber,
                                    uint32_t activeCheckerboardField,
                                    uint32_t bCheckerboardPacked);

void SetupReSTIRRemodulatePass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               const Core::ViewFamily& viewFamily,
                               Core::Array<uint32_t, 2> renderExtent,
                               const RenderTargets& targets,
                               uint32_t sceneIndex,
                               uint32_t outputMode,
                               float iblIntensity,
                               uint64_t frameNumber,
                               bool bDDGIApply,
                               const Core::RTReflectionConfiguration& reflectionConfig);
} // Render

#endif //WILL_ENGINE_RESTIR_PASSES_H
