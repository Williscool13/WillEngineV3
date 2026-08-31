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

/** Descriptor buffer + noisy output name shared between the ReSTIR-mode base-pass side-output producer and the Default-mode dedicated trace producer. */
inline const StringID REFLECTION_HIT_DESCRIPTORS_BUFFER = SID("reflection_hit_descriptors");
inline const StringID REFLECTION_SPEC_NOISY_TARGET = SID("reflection_spec_noisy");
inline const StringID REFLECTION_HIT_DELTA_TARGET = SID("reflection_hit_delta");
inline const StringID REFLECTION_HIT_DELTA_HISTORY = SID("reflection_hit_delta_history");

inline float ComputeReflectionRoughnessMax(const Core::ReflectionConfiguration& config)
{
    return config.bEnabled ? config.tracedRoughnessMax : -1.0f;
}

/**
 * Creates and fills the reflection descriptor buffer with a dedicated GGX ray per pixel, for lighting modes that have no ReSTIR BRDF ray to
 * piggyback. Every pixel is written, so no clear pass is required. No-op when reflections are disabled or no TLAS exists.
 */
void SetupReflectionTracePass(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex,
                              uint64_t frameNumber,
                              const Core::ReflectionConfiguration& reflectionConfig);

/**
 * Screen-space alternative to SetupReflectionTracePass: marches the same GGX ray against depth_copy instead of the TLAS, writing the shared
 * reflection descriptor buffer with the REFLECTION_INSTANCE_NONE sentinel. Every pixel is written, so no clear pass is required. Requires no TLAS.
 */
void SetupSSRTracePass(RenderGraph& graph,
                       PipelineManager* pipelineManager,
                       Core::Array<uint32_t, 2> renderExtent,
                       const RenderTargets& targets,
                       uint32_t sceneIndex,
                       uint64_t frameNumber,
                       uint32_t activeCheckerboardField,
                       const Core::ReflectionConfiguration& reflectionConfig);

/** Shades each hit in the reflection descriptor buffer (sun + one NEE light + emissive + DDGI irradiance, reusing ShadeProbeRayHit); sky misses sample the skybox; ReSTIR-owned hits contribute nothing. Demodulated output; no-op when disabled. */
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
