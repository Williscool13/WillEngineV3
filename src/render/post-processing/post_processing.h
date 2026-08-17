//
// Created by William on 2026-04-19.
//

#ifndef WILLENGINEV3_POST_PROCESSING_H
#define WILLENGINEV3_POST_PROCESSING_H

#include <cstdint>

#include "core/containers/array.h"
#include "core/string_id.h"
#include "render/renderer_types.h"

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
    const Core::ViewFamily& view;
    Core::Array<uint32_t, 2> extent;
    Core::Array<uint32_t, 2> preAaExtent;
    Core::Array<uint32_t, 2> displayExtent;
    float deltaTime;
    uint64_t frameNumber;
    PipelineManager* pipelines;
};

// Sideband passes: produce named side resources, return input unchanged
StringID PPExposure(PostProcessContext& ctx, StringID input);
StringID PPBloom(PostProcessContext& ctx, StringID input);
/** Half-res dof_color_coc: rgb = color, a = signed CoC in full-res pixels, negative in front of the focal plane. */
StringID PPDepthOfField(PostProcessContext& ctx, StringID input);

// Transform passes: 1-in 1-out, return their output name
StringID PPMotionBlur(PostProcessContext& ctx, StringID input);
// Fused panini remap + chromatic aberration + bloom composite + exposure + tonemap + grading + vignette.
StringID PPFinalize(PostProcessContext& ctx, StringID input);
// Display-referred sharpen + film grain + sRGB-step dither.
StringID PPCompose(PostProcessContext& ctx, StringID input);
// Gameplay screen cover (fade/iris/wipe/dissolve/letterbox); skipped entirely when inactive.
// Outside the chain above because ScreenFadeState::bDrawOverUI decides whether it runs before or after UI compositing.
StringID PPScreenFade(RenderGraph& graph, PipelineManager* pipelines, const Core::ScreenFadeState& fade, Core::Array<uint32_t, 2> extent, StringID input);
} // Render

#endif //WILLENGINEV3_POST_PROCESSING_H
