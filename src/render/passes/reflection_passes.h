//
// Created by William on 2026-07-09.
//

#ifndef WILL_ENGINE_REFLECTION_PASSES_H
#define WILL_ENGINE_REFLECTION_PASSES_H

#include <glm/glm.hpp>

#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/shaders/restir_interop.h"
#include "render/interface/render_interface.h"
#include "core/types/extent.h"

namespace Core
{
struct ViewFamily;
}

namespace Render
{
class PipelineManager;

inline const StringID REFLECTION_HIT_DESCRIPTORS_BUFFER = "reflection_hit_descriptors"_sid;
inline const StringID REFLECTION_SPEC_NOISY_TARGET = "reflection_spec_noisy"_sid;
inline const StringID REFLECTION_HIT_DELTA_TARGET = "reflection_hit_delta"_sid;
inline const StringID REFLECTION_VIRTUAL_MOTION_TARGET = "reflection_virtual_motion"_sid;

inline float ComputeReflectionRoughnessMax(const Core::ReflectionConfiguration& config)
{
    return config.bEnabled ? config.tracedRoughnessMax : -1.0f;
}

inline float ComputeLightSpecularFromReflectionsMax(const Core::ReflectionConfiguration& config)
{
    if (!config.bEnabled) {
        return -1.0f;
    }
    return config.lightSpecularFromReflectionsMax < config.tracedRoughnessMax ? config.lightSpecularFromReflectionsMax : config.tracedRoughnessMax;
}

/**
 * RT trace of the reflection BRDF ray, used when ReSTIR does not piggyback it.
 * @return the frame with hitDescriptors; invalid hitDescriptors when reflections are off or there is no TLAS
 */
ReflectionFrame SetupReflectionTracePass(RenderGraph& graph,
                                         PipelineManager* pipelineManager,
                                         Core::Extent2D renderExtent,
                                         const RenderTargets& targets,
                                         const SceneResources& scene,
                                         uint32_t sceneIndex,
                                         uint64_t frameNumber,
                                         const Core::ReflectionConfiguration& reflectionConfig);

/**
 * Hits carry the REFLECTION_INSTANCE_NONE sentinel.
 * @return the frame with hitDescriptors; invalid hitDescriptors when reflections are off
 */
ReflectionFrame SetupSSRTracePass(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  Core::Extent2D renderExtent,
                                  const RenderTargets& targets,
                                  const SceneResources& scene,
                                  uint32_t sceneIndex,
                                  uint64_t frameNumber,
                                  uint32_t activeCheckerboardField,
                                  const Core::ReflectionConfiguration& reflectionConfig);

/**
 * ReSTIR-owned hits contribute nothing. Output is demodulated.
 * @param trace the trace output (SetupReflectionTracePass, SetupSSRTracePass, or SetupReSTIRPasses' piggyback); nothing is recorded without hitDescriptors
 * @return trace completed with specNoisy and, under merged denoise with a TLAS, hitDelta/hitDeltaHistory
 */
ReflectionFrame SetupReflectionShadePass(RenderGraph& graph,
                                         PipelineManager* pipelineManager,
                                         const Core::ViewFamily& viewFamily,
                                         Core::Extent2D renderExtent,
                                         const RenderTargets& targets,
                                         const SceneResources& scene,
                                         const ReflectionFrame& trace,
                                         const DDGIFrame& ddgi,
                                         const WorldGridFrame& worldGrid,
                                         uint32_t sceneIndex,
                                         uint64_t frameNumber,
                                         uint32_t activeCheckerboardField,
                                         const Core::ReflectionConfiguration& reflectionConfig,
                                         bool bDDGIApply,
                                         bool bCheckerboardPacked,
                                         bool bDisableScreenTier);
} // Render

#endif //WILL_ENGINE_REFLECTION_PASSES_H
