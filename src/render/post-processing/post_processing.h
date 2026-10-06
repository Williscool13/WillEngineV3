//
// Created by William on 2026-04-19.
//

#ifndef WILLENGINEV3_POST_PROCESSING_H
#define WILLENGINEV3_POST_PROCESSING_H

#include <cstdint>

#include "core/containers/array.h"
#include "core/string_id.h"
#include "render/frame_outputs.h"
#include "render/renderer_types.h"
#include "core/types/extent.h"

namespace Core
{
struct PostProcessConfiguration;
struct ScreenFadeState;
struct ViewFamily;
}

namespace Render
{
class RenderGraph;
class PipelineManager;

struct PostProcessContext
{
    RenderGraph& graph;
    const Core::PostProcessConfiguration& config;
    const RenderTargets& targets;
    const SceneResources& scene;
    const Core::ViewFamily& view;
    Core::Extent2D extent;
    Core::Extent2D preAaExtent;
    Core::Extent2D displayExtent;
    float deltaTime;
    float preExposure;
    uint64_t frameNumber;
    PipelineManager* pipelines;
    // Written by PPBloom, read by PPFinalize
    RDGTexture bloomChain{};
};

struct PaniniParams
{
    float strength{0.0f};
    float b{0.0f};
    float verticalFocalLength{0.0f};
};

/** @returns the Panini constants PPFinalize uses; strength 0 is rectilinear. */
PaniniParams ComputePaniniParams(const Core::PostProcessConfiguration& config, float fovRadians, float aspect);

/**
 * Maps a displayed-image UV to the rectilinear source UV the finalize pass samples there.
 * @return false if that display point shows no source pixel.
 */
bool PaniniDisplayToSourceUv(const PaniniParams& panini, float aspect, float& u, float& v);

/** @returns the average scene luminance that meters to ev100 (ISO 100, K = 12.5). */
float EV100ToLuminance(float ev100);

/** @returns the EV100 of the manual or physical camera; auto mode meters instead. */
float CameraEV100(const Core::PostProcessConfiguration& config);

// Sideband passes: produce named side resources, return input unchanged
RDGTexture PPExposure(PostProcessContext& ctx, RDGTexture input);
RDGTexture PPBloom(PostProcessContext& ctx, RDGTexture input);

// Transform passes: 1-in 1-out, return their output name
RDGTexture PPMotionBlur(PostProcessContext& ctx, RDGTexture input);
// Fused panini remap + chromatic aberration + bloom composite + exposure + tonemap + grading + vignette.
RDGTexture PPFinalize(PostProcessContext& ctx, RDGTexture input);
// Display-referred sharpen + film grain + sRGB-step dither.
RDGTexture PPCompose(PostProcessContext& ctx, RDGTexture input);
// Gameplay screen cover (fade/iris/wipe/dissolve/letterbox); skipped entirely when inactive.
// Outside the chain above because ScreenFadeState::bDrawOverUI decides whether it runs before or after UI compositing.
RDGTexture PPScreenFade(RenderGraph& graph, PipelineManager* pipelines, const Core::ScreenFadeState& fade, Core::Extent2D extent, RDGTexture input);
RDGTexture PPDepthOfField(RenderGraph& graph, PipelineManager* pipelines, const Core::PostProcessConfiguration& config, const RenderTargets& targets, const SceneResources& scene, Core::Extent2D extent,
                          uint64_t frameNumber, RDGTexture input);
} // Render

#endif //WILLENGINEV3_POST_PROCESSING_H
