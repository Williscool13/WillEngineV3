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
inline const StringID REFLECTION_SPEC_DENOISED_TARGET = SID("reflection_spec_denoised");

/** The dedicated trace produces its own ray at any roughness, so nothing clamps the slider; pass this where a piggybacked producer would supply brdfRoughnessMax. */
inline constexpr float REFLECTION_NO_BRDF_CLAMP = FLT_MAX;

/** Piggybacked BRDF ray means no ray exists past brdfRoughnessMax regardless of the slider; negative return disables (roughness is never negative). */
inline float ComputeReflectionRoughnessMax(const Core::RTReflectionConfiguration& reflectionConfig, float brdfRoughnessMax)
{
    return reflectionConfig.bEnabled ? glm::min(reflectionConfig.roughnessMax, brdfRoughnessMax) : -1.0f;
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
                              const Core::RTReflectionConfiguration& reflectionConfig);

/** Shades each hit in the reflection descriptor buffer (sun + one NEE light + emissive + DDGI irradiance, reusing ShadeProbeRayHit); sky misses sample the skybox; ReSTIR-owned hits contribute nothing. Demodulated output; no-op when disabled. */
void SetupReflectionShadePass(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex,
                              uint64_t frameNumber,
                              uint32_t activeCheckerboardField,
                              const Core::RTReflectionConfiguration& reflectionConfig,
                              bool bDDGIApply,
                              bool bCheckerboardPacked,
                              float brdfRoughnessMax);
} // Render

#endif //WILL_ENGINE_REFLECTION_PASSES_H
