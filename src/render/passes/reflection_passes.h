//
// Created by William on 2026-07-09.
//

#ifndef WILL_ENGINE_REFLECTION_PASSES_H
#define WILL_ENGINE_REFLECTION_PASSES_H

#include <glm/glm.hpp>

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"
#include "render/shaders/restir_interop.h"
#include "render/interface/render_interface.h"

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

void SetupReflectionTracePass(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex,
                              uint64_t frameNumber,
                              const Core::ReflectionConfiguration& reflectionConfig);

/** Hits carry the REFLECTION_INSTANCE_NONE sentinel. */
void SetupSSRTracePass(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex,
                       uint64_t frameNumber,
                       uint32_t activeCheckerboardField,
                       const Core::ReflectionConfiguration& reflectionConfig);

/** ReSTIR-owned hits contribute nothing. Output is demodulated. */
void SetupReflectionShadePass(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex,
                              uint64_t frameNumber,
                              uint32_t activeCheckerboardField,
                              const Core::ReflectionConfiguration& reflectionConfig,
                              bool bDDGIApply,
                              bool bCheckerboardPacked,
                              bool bDisableScreenTier);
} // Render

#endif //WILL_ENGINE_REFLECTION_PASSES_H
