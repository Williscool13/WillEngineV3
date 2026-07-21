//
// Created by William on 2026-06-03.
//

#ifndef WILL_ENGINE_SHADOW_PASSES_H
#define WILL_ENGINE_SHADOW_PASSES_H

#include "render/renderer_types.h"
#include "render/render-graph/render_graph.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;

inline const StringID HERO_SUN_SHADOW_TARGET = SID("hero_sun_shadow");
inline const StringID HERO_SUN_SHADOW_HISTORY = SID("hero_sun_shadow_history");

void SetupShadowsResolve(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Array<uint32_t, 2> renderExtent,
                         const RenderTargets& targets,
                         uint32_t sceneIndex);

/**
 * @brief Deterministic sun shadow cast by hero instances, which the traced sun shadow excludes.
 * Fixed sun-disk quadrature against the hero BLASes only, so the result is noise-free and needs no denoiser.
 * Writes full-res visibility to hero_sun_shadow. No-ops without a TLAS, a sun, or a hero in the scene.
 */
void SetupHeroSunShadow(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Array<uint32_t, 2> renderExtent,
                        const RenderTargets& targets,
                        uint32_t sceneIndex);

/**
 * SIGMA shadow denoiser (penumbra-aware spatial filter) over the rt_sun_shadow signal.
 * Writes denoised (visibility, penumbra) to sigma_shadow. No-ops if rt_sun_shadow is absent.
 */
void SetupSigmaShadowDenoise(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Array<uint32_t, 2> renderExtent,
                             const RenderTargets& targets,
                             uint32_t sceneIndex,
                             uint64_t frameNumber);

/**
 * @brief SIGMA temporal stabilization: motion-vector reproject + neighborhood-clamp the previous
 * sigma_shadow result. Writes sigma_stabilized and carries it to next frame. No-ops if sigma_shadow is absent.
 */
void SetupSigmaShadowTemporal(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Array<uint32_t, 2> renderExtent,
                              const RenderTargets& targets,
                              uint32_t sceneIndex);
} // Render

#endif //WILL_ENGINE_SHADOW_PASSES_H
