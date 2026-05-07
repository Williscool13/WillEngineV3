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
    const MainRenderTargets& targets;
    const Core::ViewFamily& view;
    Core::Array<uint32_t, 2> extent;
    float deltaTime;
    uint64_t frameNumber;
    PipelineManager* pipelines;
};

// Sideband passes: produce named side resources, return input unchanged
StringID PPExposure(PostProcessContext& ctx, StringID input);
StringID PPBloom(PostProcessContext& ctx, StringID input);

// Transform passes: 1-in 1-out, return their output name
StringID PPSharpening(PostProcessContext& ctx, StringID input);
StringID PPTonemap(PostProcessContext& ctx, StringID input);
StringID PPMotionBlur(PostProcessContext& ctx, StringID input);
StringID PPColorGrading(PostProcessContext& ctx, StringID input);
StringID PPVignetteAberration(PostProcessContext& ctx, StringID input);
StringID PPPanini(PostProcessContext& ctx, StringID input);
StringID PPFilmGrain(PostProcessContext& ctx, StringID input);
StringID PPDither(PostProcessContext& ctx, StringID input);

} // Render

#endif //WILLENGINEV3_POST_PROCESSING_H
