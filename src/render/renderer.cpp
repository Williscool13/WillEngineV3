//
// Created by William on 2026-04-16.
//

#include "render/renderer.h"

#include "render/post-processing/post_processing.h"

namespace Render
{
RDGTexture SetupPostProcessing(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::ViewFamily& viewFamily,
                             Core::Extent2D renderExtent,
                             Core::Extent2D preAaExtent,
                             Core::Extent2D displayExtent,
                             const RenderTargets& targets,
                             const SceneResources& scene,
                             float deltaTime,
                             uint64_t frameNumber,
                             float preExposure)
{
    PostProcessContext ctx{
        .graph = graph,
        .config = viewFamily.postProcessConfig,
        .targets = targets,
        .scene = scene,
        .view = viewFamily,
        .extent = renderExtent,
        .preAaExtent = preAaExtent,
        .displayExtent = displayExtent,
        .deltaTime = deltaTime,
        .preExposure = preExposure,
        .frameNumber = frameNumber,
        .pipelines = pipelineManager,
    };

    RDGTexture current = ctx.targets.colorOutput;
    current = PPExposure(ctx, current);
    current = PPMotionBlur(ctx, current);
    current = PPBloom(ctx, current);
    current = PPFinalize(ctx, current);
    current = PPCompose(ctx, current);
    return current;
}
} // Render
