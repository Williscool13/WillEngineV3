//
// Created by William on 2026-01-21.
//

#ifndef WILL_ENGINE_RENDER_VIEW_HELPERS_H
#define WILL_ENGINE_RENDER_VIEW_HELPERS_H
#include "core/containers/array.h"
#include "core/memory/arena.h"
#include "render/interface/render_interface.h"
#include "render/render_config.h"
#include "render/shaders/common_interop.h"
#include "render/types/render_types.h"

namespace Render
{
class PipelineManager;
struct FrameResourceLimits;

/**
 * Generate scene data based on information from the render view among other things like AA mode
 * @param view
 * @param aaConfig
 * @param renderExtent
 * @param frameNumber
 * @param deltaTime
 * @return
 */
SceneData GenerateSceneData(const Core::RenderView& view, const Core::AntiAliasingConfiguration& aaConfig, Core::Array<uint32_t, 2> renderExtent, uint64_t frameNumber, float deltaTime, float resolutionScale);

uint32_t ComputeJitterPhaseCount(Core::AntiAliasingMode aaMode, float resolutionScale);

HaltonSample ComputeJitterSample(Core::AntiAliasingMode aaMode, uint64_t frameNumber, uint32_t jitterPhaseCount);

/**
 * NRD's checkerboard resolve accumulation speed (InstanceImpl.cpp): lerp(nonLinearAccumSpeed, 0.5, jitterDelta),
 * where nonLinearAccumSpeed is FPS-driven and jitterDelta is the per-frame camera-jitter movement in pixels (0 when not jittering).
 */
float ComputeCheckerboardResolveAccumSpeed(Core::AntiAliasingMode aaMode, uint64_t frameNumber, float renderFps, float resolutionScale);

/**
 * NRD's per-frame max camera-jitter delta in pixels (0 when not jittering).
 */
float ComputeRelaxJitterDelta(Core::AntiAliasingMode aaMode, uint64_t frameNumber, float resolutionScale);

/**
 * Clean up some invalid fields in the view family. E.g. materials w/out compiled shaders (at the time of draw)
 * @param viewFamily
 * @param pipelineManager used to validate shader pipeline existence
 * @param arena scratch arena for the per-call resolved-shader cache
 */
void SanitizeViewFamily(Core::ViewFamily& viewFamily, PipelineManager* pipelineManager, Core::Arena* arena);

/**
 * Prepare render family with transient data needed for rendering down the lime. E.g. text draw call batching.
 * @param viewFamily
 */
void PrepareRenderFamily(Core::ViewFamily& viewFamily);

/**
 * Calculate limits for geometry and rendering buffers
 * @param viewFamily
 * @param readbackData
 * @param _pipelineManager
 * @param _limits
 * @return
 */
RenderFamilyProperties PrepareRenderFamilyProperties(Core::ViewFamily& viewFamily, ReadbackStruct* readbackData, PipelineManager* _pipelineManager, FrameResourceLimits& _limits);

} // Render

#endif //WILL_ENGINE_RENDER_VIEW_HELPERS_H
