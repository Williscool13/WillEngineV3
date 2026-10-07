//
// Created by William on 2026-06-06.
//

#ifndef WILL_ENGINE_RESTIR_PASSES_H
#define WILL_ENGINE_RESTIR_PASSES_H

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/shaders/ddgi_interop.h"
#include "core/types/extent.h"

namespace Core
{
struct ViewFamily;
struct Arena;
struct ReflectionConfiguration;
}

namespace Render
{
class PipelineManager;

/**
 * Light transform, ReGIR, reservoir generation/reuse, sun visibility and confidence.
 * @param reflection receives hitDescriptors when the base pass piggybacks the reflection BRDF ray (bSkipReflectionPiggyback false, reflections enabled); left untouched otherwise
 * @return every ReSTIR output this frame produced
 */
ReSTIRFrame SetupReSTIRPasses(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Extent2D renderExtent,
                              const RenderTargets& targets,
                              const SceneResources& scene,
                              const WorldGridFrame& worldGrid,
                              uint32_t sceneIndex,
                              Core::Arena& arena,
                              uint64_t frameNumber,
                              const Core::ReSTIRParams& restirParams,
                              uint32_t activeCheckerboardField,
                              const Core::ReflectionConfiguration& reflectionConfig,
                              bool bResetHistory,
                              bool bSkipReflectionPiggyback,
                              float preExposure,
                              ReflectionFrame& reflection);

void SetupReSTIRLightingResolvePass(RenderGraph& graph,
                                    PipelineManager* pipelineManager,
                                    const Core::ViewFamily& viewFamily,
                                    Core::Extent2D renderExtent,
                                    const RenderTargets& targets,
                                    const SceneResources& scene,
                                    const GeometryFrame& geometry,
                                    const ReSTIRFrame& restir,
                                    const ReflectionFrame& reflection,
                                    uint32_t sceneIndex,
                                    uint64_t frameNumber,
                                    uint32_t activeCheckerboardField,
                                    uint32_t bCheckerboardPacked,
                                    uint32_t bFullRateResolve,
                                    const Core::ReflectionConfiguration& reflectionConfig);

void SetupReSTIRRemodulatePass(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               const Core::ViewFamily& viewFamily,
                               Core::Extent2D renderExtent,
                               const RenderTargets& targets,
                               const SceneResources& scene,
                               const WorldGridFrame& worldGrid,
                               const DDGIFrame& ddgi,
                               const FinalGatherFrame& gather,
                               const ReflectionFrame& reflection,
                               uint32_t sceneIndex,
                               uint32_t outputMode,
                               float iblIntensity,
                               uint64_t frameNumber,
                               bool bDDGIApply,
                               const Core::ReflectionConfiguration& reflectionConfig,
                               uint32_t giGatherMode,
                               bool bDirectSun);
} // Render

#endif //WILL_ENGINE_RESTIR_PASSES_H
