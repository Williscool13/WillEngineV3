//
// Created by William on 2026-04-19.
//

#include "post_processing.h"

#include <algorithm>

#include "render/render_config.h"
#include "render/vulkan/vk_config.h"
#include "render/interface/render_interface.h"
#include "render/render-graph/render_graph.h"
#include "render/render-graph/render_pass.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/shaders/constants_interop.h"
#include "render/shaders/push_constant_interop.h"
#include "core/containers/inline_string.h"
#include "render/render_utils.h"

namespace Render
{
StringID PPExposure(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bExposureEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    float adaptationRate = ctx.config.exposureAdaptationRate;
    float deltaTime = ctx.deltaTime;

    graph.CreateBuffer(SID("luminance_histogram"), POST_PROCESS_LUMINANCE_BUFFER_SIZE, false);

    if (!graph.HasBuffer(SID("luminance_buffer"))) {
        graph.CreateBuffer(SID("luminance_buffer"), sizeof(float), false);
    }
    graph.CarryBufferToNextFrame(SID("luminance_buffer"), SID("luminance_buffer"), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    auto& clearPass = graph.AddPass(SID("[Exposure] Clear Histogram"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::ResourceCategory::PostProcessing);
    clearPass.WriteTransferBuffer(SID("luminance_histogram"));
    clearPass.Execute([&graph](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(SID("luminance_histogram")), 0, VK_WHOLE_SIZE, 0);
    });

    auto& histogramPass = graph.AddPass(SID("[Exposure] Build Histogram"), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    histogramPass.ReadSampledImage(input);
    histogramPass.ReadWriteBuffer(SID("luminance_histogram"));
    histogramPass.Execute([&graph, width, height, input, pipelines](VkCommandBuffer cmd) {
        constexpr float minLogLuminance = -10.0f;
        constexpr float maxLogLuminance = 2.0f;
        constexpr float logLuminanceRange = maxLogLuminance - minLogLuminance;
        constexpr float oneOverLogLuminanceRange = 1.0f / logLuminanceRange;
        HistogramBuildPushConstant pc{
            .hdrImageIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .histogramBufferAddress = graph.GetBufferAddress(SID("luminance_histogram")),
            .width = width,
            .height = height,
            .minLogLuminance = minLogLuminance,
            .oneOverLogLuminanceRange = oneOverLogLuminanceRange,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("exposure_build_histogram"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_LUMINANCE_DISPATCH_X - 1) / POST_PROCESS_LUMINANCE_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_LUMINANCE_DISPATCH_Y - 1) / POST_PROCESS_LUMINANCE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    auto& exposurePass = graph.AddPass(SID("[Exposure] Calculate Exposure"), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    exposurePass.ReadBuffer(SID("luminance_histogram"));
    exposurePass.ReadWriteBuffer(SID("luminance_buffer"));
    exposurePass.Execute([&graph, width, height, pipelines, adaptationRate, deltaTime](VkCommandBuffer cmd) {
        constexpr float minLogLuminance = -10.0f;
        constexpr float maxLogLuminance = 2.0f;
        constexpr float logLuminanceRange = maxLogLuminance - minLogLuminance;
        ExposureCalculatePushConstant pc{
            .histogramBufferAddress = graph.GetBufferAddress(SID("luminance_histogram")),
            .luminanceBufferAddress = graph.GetBufferAddress(SID("luminance_buffer")),
            .minLogLuminance = minLogLuminance,
            .logLuminanceRange = logLuminanceRange,
            .adaptationSpeed = adaptationRate * deltaTime,
            .totalPixels = width * height,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("exposure_calculate_average"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, 1, 1, 1);
    });

    return input;
}

StringID PPBloom(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bBloomEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    float bloomThreshold = ctx.config.bloomThreshold;
    float bloomSoftThreshold = ctx.config.bloomSoftThreshold;
    float bloomRadius = ctx.config.bloomRadius;
    float bloomClamp = ctx.config.bloomClamp;

    const uint32_t numDownsamples = (width >= 3840) ? 6 : 5;
    const uint32_t numMips = numDownsamples + 1;
    graph.CreateTexture(SID("bloom_chain"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, numMips}, CLEAR_COLOR_EMPTY, true);

    RenderPass& thresholdPass = graph.AddPass(SID("[Bloom] Threshold"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    thresholdPass.ReadSampledImage(input);
    thresholdPass.ReadWriteImage(SID("bloom_chain"));
    thresholdPass.Execute([&graph, width, height, input, pipelines, bloomThreshold, bloomSoftThreshold, bloomClamp](VkCommandBuffer cmd) {
        BloomThresholdPushConstant pc{
            .outputExtent = {width, height},
            .inputColorIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("bloom_chain"), 0),
            .threshold = bloomThreshold,
            .softThreshold = bloomSoftThreshold,
            .clampValue = bloomClamp,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("bloom_threshold"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    for (uint32_t i = 0; i < numDownsamples; ++i) {
        uint32_t mipWidth = std::max(1u, width >> (i + 1));
        uint32_t mipHeight = std::max(1u, height >> (i + 1));

        Core::InlineString<32> passName;
        passName.len = snprintf(passName.buf, 32, "[Bloom] Downsample %u", i);
        RenderPass& downsamplePass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
        downsamplePass.ReadWriteImage(SID("bloom_chain"));
        downsamplePass.Execute([&graph, mipWidth, mipHeight, srcMip = i, dstMip = i + 1, pipelines](VkCommandBuffer cmd) {
            BloomDownsamplePushConstant pc{
                .outputExtent = {mipWidth, mipHeight},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("bloom_chain")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("bloom_chain"), dstMip),
                .srcMipLevel = srcMip,
            };

            const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("bloom_downsample"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (mipWidth + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
            uint32_t yDispatch = (mipHeight + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    for (int32_t i = static_cast<int32_t>(numDownsamples) - 1; i >= 0; --i) {
        uint32_t mipWidth = std::max(1u, width >> i);
        uint32_t mipHeight = std::max(1u, height >> i);

        Core::InlineString<32> passName;
        passName.len = snprintf(passName.buf, 32, "[Bloom] Upsample %d", i);
        RenderPass& upsamplePass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
        upsamplePass.ReadWriteImage(SID("bloom_chain"));
        upsamplePass.Execute([&graph, mipWidth, mipHeight, dstMip = i, lowerMip = i + 1, pipelines, bloomRadius](VkCommandBuffer cmd) {
            BloomUpsamplePushConstant pc{
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("bloom_chain")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("bloom_chain"), dstMip),
                .lowerMipLevel = static_cast<uint32_t>(lowerMip),
                .higherMipLevel = static_cast<uint32_t>(dstMip),
                .radius = bloomRadius,
            };

            const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("bloom_upsample"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (mipWidth + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
            uint32_t yDispatch = (mipHeight + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    return input;
}

StringID PPSharpening(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bSharpeningEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    float sharpness = ctx.config.sharpeningStrength;

    graph.CreateTexture(SID("sharpening_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, CLEAR_COLOR_EMPTY, true);
    RenderPass& sharpeningPass = graph.AddPass(SID("[Sharpening] Apply"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    sharpeningPass.ReadBuffer(SID("scene_data"));
    sharpeningPass.ReadSampledImage(input);
    sharpeningPass.WriteStorageImage(SID("sharpening_output"));
    sharpeningPass.Execute([&graph, width, height, input, pipelines, sharpness](VkCommandBuffer cmd) {
        SharpeningPushConstant pc{
            .outputExtent = {width, height},
            .inputIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("sharpening_output")),
            .sharpness = sharpness,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("sharpening"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_SHARPENING_DISPATCH_X - 1) / POST_PROCESS_SHARPENING_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_SHARPENING_DISPATCH_Y - 1) / POST_PROCESS_SHARPENING_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("sharpening_output");
}

StringID PPTonemap(PostProcessContext& ctx, StringID input)
{
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    int32_t tonemapOperator = ctx.config.tonemapOperator;
    float targetLuminance = ctx.config.exposureTargetLuminance;
    float bloomIntensity = ctx.config.bloomIntensity;

    bool bBloomEnabled = ctx.config.bBloomEnabled;
    bool bExposureEnabled = ctx.config.bExposureEnabled;

    float tonemapParams[6]{};
    switch (tonemapOperator) {
        case 1: tonemapParams[0] = ctx.config.hableParams.whitePoint; break;
        case 2: tonemapParams[0] = ctx.config.reinhardParams.whitePoint; break;
        case 7:
            tonemapParams[0] = ctx.config.uchimuraParams.P;
            tonemapParams[1] = ctx.config.uchimuraParams.a;
            tonemapParams[2] = ctx.config.uchimuraParams.m;
            tonemapParams[3] = ctx.config.uchimuraParams.l;
            tonemapParams[4] = ctx.config.uchimuraParams.c;
            tonemapParams[5] = ctx.config.uchimuraParams.b;
            break;
        case 9: tonemapParams[0] = ctx.config.agxParams.minEV; tonemapParams[1] = ctx.config.agxParams.maxEV; break;
        case 10: tonemapParams[0] = ctx.config.khronosParams.startCompression; tonemapParams[1] = ctx.config.khronosParams.desaturation; break;
        default: break;
    }

    // todo: add support for HDR swapchain
    graph.CreateTexture(SID("tonemap_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, CLEAR_COLOR_EMPTY, true);
    RenderPass& tonemapPass = graph.AddPass(SID("[Tonemap] SDR"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    tonemapPass.ReadSampledImage(input);
    if (bBloomEnabled) { tonemapPass.ReadSampledImage(SID("bloom_chain")); }
    if (bExposureEnabled) { tonemapPass.ReadBuffer(SID("luminance_buffer")); }
    tonemapPass.WriteStorageImage(SID("tonemap_output"));
    tonemapPass.Execute([&graph, width, height, input, pipelines, tonemapOperator, targetLuminance, bloomIntensity, tonemapParams, bBloomEnabled, bExposureEnabled](VkCommandBuffer cmd) {
        TonemapSDRPushConstant pc{
            .tonemapOperator = tonemapOperator,
            .targetLuminance = targetLuminance,
            .luminanceBufferAddress = bExposureEnabled ? graph.GetBufferAddress(SID("luminance_buffer")) : 0,
            .bloomImageIndex = bBloomEnabled ? graph.GetSampledImageViewDescriptorIndex(SID("bloom_chain")) : 0u,
            .bloomIntensity = bloomIntensity,
            .outputWidth = width,
            .outputHeight = height,
            .srcImageIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .dstImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("tonemap_output")),
            .bBloomEnabled = bBloomEnabled ? 1 : 0,
            .bExposureEnabled = bExposureEnabled ? 1 : 0,
        };
        memcpy(pc.params, tonemapParams, sizeof(pc.params));

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("tonemap_sdr"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TonemapSDRPushConstant), &pc);
        uint32_t xDispatch = (width + 15) / 16;
        uint32_t yDispatch = (height + 15) / 16;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("tonemap_output");
}

StringID PPMotionBlur(PostProcessContext& ctx, StringID input)
{
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    StringID velocity = ctx.targets.gbufferTwo;
    StringID depthStencil = ctx.targets.depthStencil;
    float velocityScale = ctx.config.motionBlurVelocityScale;
    float depthScale = ctx.config.motionBlurDepthScale;

    uint32_t blurTiledX = (width + POST_PROCESS_MOTION_BLUR_TILE_SIZE - 1) / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
    uint32_t blurTiledY = (height + POST_PROCESS_MOTION_BLUR_TILE_SIZE - 1) / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
    graph.CreateTexture(SID("motion_blur_tiled_max"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, blurTiledX, blurTiledY, 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("motion_blur_tiled_neighbor_max"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, blurTiledX, blurTiledY, 1}, CLEAR_COLOR_EMPTY, true);
    graph.CreateTexture(SID("motion_blur_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, CLEAR_COLOR_EMPTY, true);

    RenderPass& motionBlurTiledMaxPass = graph.AddPass(SID("[Motion Blur] Tiled Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    motionBlurTiledMaxPass.ReadBuffer(SID("scene_data"));
    motionBlurTiledMaxPass.ReadSampledImage(velocity);
    motionBlurTiledMaxPass.WriteStorageImage(SID("motion_blur_tiled_max"));
    motionBlurTiledMaxPass.Execute([&graph, width, height, blurTiledX, blurTiledY, pipelines, velocity](VkCommandBuffer cmd) {
        MotionBlurTileVelocityPushConstant pc{
            .velocityBufferSize = {width, height},
            .tileBufferSize = {blurTiledX, blurTiledY},
            .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex(velocity),
            .tileMaxIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_tiled_max")),
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("motion_blur_tile_max"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_X;
        uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_TILE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& motionBlurNeighborMax = graph.AddPass(SID("[Motion Blur] Neighbor Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    motionBlurNeighborMax.ReadSampledImage(SID("motion_blur_tiled_max"));
    motionBlurNeighborMax.WriteStorageImage(SID("motion_blur_tiled_neighbor_max"));
    motionBlurNeighborMax.Execute([&graph, blurTiledX, blurTiledY, pipelines](VkCommandBuffer cmd) {
        MotionBlurNeighborMaxPushConstant pc{
            .tileBufferSize = {blurTiledX, blurTiledY},
            .tileMaxIndex = graph.GetSampledImageViewDescriptorIndex(SID("motion_blur_tiled_max")),
            .neighborMaxIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_tiled_neighbor_max")),
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("motion_blur_neighbor_max"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X;
        uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    RenderPass& motionBlurReconstructionPass = graph.AddPass(SID("[Motion Blur] Reconstruction"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    motionBlurReconstructionPass.ReadBuffer(SID("scene_data"));
    motionBlurReconstructionPass.ReadSampledImage(input);
    motionBlurReconstructionPass.ReadSampledImage(velocity);
    motionBlurReconstructionPass.ReadSampledImage(depthStencil);
    motionBlurReconstructionPass.ReadSampledImage(SID("motion_blur_tiled_neighbor_max"));
    motionBlurReconstructionPass.WriteStorageImage(SID("motion_blur_output"));
    motionBlurReconstructionPass.Execute([&graph, width, height, input, pipelines, velocity, depthStencil, velocityScale, depthScale](VkCommandBuffer cmd) {
        MotionBlurReconstructionPushConstant pc{
            .srcBufferSize = {width, height},
            .sceneColorIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex(velocity),
            .depthBufferIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depthStencil),
            .tileNeighborMaxIndex = graph.GetSampledImageViewDescriptorIndex(SID("motion_blur_tiled_neighbor_max")),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_output")),
            .velocityScale = velocityScale,
            .depthScale = depthScale,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("motion_blur_reconstruction"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_MOTION_BLUR_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_MOTION_BLUR_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("motion_blur_output");
}

StringID PPColorGrading(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bColorGradingEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    float exposure = ctx.config.colorGradingExposure;
    float contrast = ctx.config.colorGradingContrast;
    float saturation = ctx.config.colorGradingSaturation;
    float temperature = ctx.config.colorGradingTemperature;
    float tint = ctx.config.colorGradingTint;

    graph.CreateTexture(SID("color_grading_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, CLEAR_COLOR_EMPTY, true);
    RenderPass& colorGradingPass = graph.AddPass(SID("[Color Grading] Apply"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    colorGradingPass.ReadBuffer(SID("scene_data"));
    colorGradingPass.ReadSampledImage(input);
    colorGradingPass.WriteStorageImage(SID("color_grading_output"));
    colorGradingPass.Execute([&graph, width, height, input, pipelines, exposure, contrast, saturation, temperature, tint](VkCommandBuffer cmd) {
        ColorGradingPushConstant pc{
            .outputExtent = {width, height},
            .inputIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("color_grading_output")),
            .exposure = exposure,
            .contrast = contrast,
            .saturation = saturation,
            .temperature = temperature,
            .tint = tint,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("color_grading"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_COLOR_GRADING_DISPATCH_X - 1) / POST_PROCESS_COLOR_GRADING_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_COLOR_GRADING_DISPATCH_Y - 1) / POST_PROCESS_COLOR_GRADING_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("color_grading_output");
}

StringID PPVignetteAberration(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bVignetteAberrationEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    float chromaticAberrationStrength = ctx.config.chromaticAberrationStrength;
    float vignetteStrength = ctx.config.vignetteStrength;
    float vignetteRadius = ctx.config.vignetteRadius;
    float vignetteSmoothness = ctx.config.vignetteSmoothness;

    graph.CreateTexture(SID("vignette_aberration_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, CLEAR_COLOR_EMPTY, true);
    RenderPass& vignetteAberrationPass = graph.AddPass(SID("[Vignette] Apply"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    vignetteAberrationPass.ReadBuffer(SID("scene_data"));
    vignetteAberrationPass.ReadSampledImage(input);
    vignetteAberrationPass.WriteStorageImage(SID("vignette_aberration_output"));
    vignetteAberrationPass.Execute([&graph, width, height, input, pipelines,
                                    chromaticAberrationStrength, vignetteStrength, vignetteRadius, vignetteSmoothness](VkCommandBuffer cmd) {
        VignetteChromaticAberrationPushConstant pc{
            .outputExtent = {width, height},
            .inputIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("vignette_aberration_output")),
            .chromaticAberrationStrength = chromaticAberrationStrength,
            .vignetteStrength = vignetteStrength,
            .vignetteRadius = vignetteRadius,
            .vignetteSmoothness = vignetteSmoothness,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("vignette_aberration"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_X - 1) / POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_Y - 1) / POST_PROCESS_VIGNETTE_ABERRATION_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("vignette_aberration_output");
}

StringID PPPanini(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bPaniniEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    float fov = ctx.view.mainView.currentViewData.fovRadians;
    float aspect = ctx.view.mainView.currentViewData.aspectRatio;
    float paniniStrength = ctx.config.paniniStrength;

    graph.CreateTexture(SID("panini_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, CLEAR_COLOR_EMPTY, true);
    RenderPass& paniniPass = graph.AddPass(SID("[Panini] Apply"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    paniniPass.ReadBuffer(SID("scene_data"));
    paniniPass.ReadSampledImage(input);
    paniniPass.WriteStorageImage(SID("panini_output"));
    paniniPass.Execute([&graph, width, height, input, pipelines, fov, aspect, paniniStrength](VkCommandBuffer cmd) {
        PaniniProjectionPushConstant pc{
            .outputExtent = {width, height},
            .inputIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("panini_output")),
            .perspectiveFov = fov,
            .aspect = aspect,
            .strength = paniniStrength,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("panini_projection"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_PANINI_DISPATCH_X - 1) / POST_PROCESS_PANINI_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_PANINI_DISPATCH_Y - 1) / POST_PROCESS_PANINI_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("panini_output");
}

StringID PPFilmGrain(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bFilmGrainEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    float grainStrength = ctx.config.grainStrength;
    float grainSize = ctx.config.grainSize;
    uint32_t frameIndex = static_cast<uint32_t>(ctx.frameNumber);

    graph.CreateTexture(SID("post_process_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, CLEAR_COLOR_EMPTY, true);
    RenderPass& filmGrainPass = graph.AddPass(SID("[Film Grain] Apply"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    filmGrainPass.ReadBuffer(SID("scene_data"));
    filmGrainPass.ReadSampledImage(input);
    filmGrainPass.WriteStorageImage(SID("post_process_output"));
    filmGrainPass.Execute([&graph, width, height, input, pipelines, grainStrength, grainSize, frameIndex](VkCommandBuffer cmd) {
        FilmGrainPushConstant pc{
            .outputExtent = {width, height},
            .inputIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("post_process_output")),
            .grainStrength = grainStrength,
            .grainSize = grainSize,
            .frameIndex = frameIndex,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("film_grain"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_FILM_GRAIN_DISPATCH_X - 1) / POST_PROCESS_FILM_GRAIN_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_FILM_GRAIN_DISPATCH_Y - 1) / POST_PROCESS_FILM_GRAIN_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("post_process_output");
}

StringID PPDither(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bDitherEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    float ditherStrength = ctx.config.ditherStrength;

    graph.CreateTexture(SID("dither_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, CLEAR_COLOR_EMPTY, true);
    RenderPass& ditherPass = graph.AddPass(SID("[Dither] Apply"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::PostProcessing);
    ditherPass.ReadSampledImage(input);
    ditherPass.WriteStorageImage(SID("dither_output"));
    ditherPass.Execute([&graph, width, height, input, pipelines, ditherStrength, frameIndex = static_cast<float>(ctx.frameNumber)](VkCommandBuffer cmd) {
        DitherPushConstant pc{
            .outputExtent = {width, height},
            .inputIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("dither_output")),
            .ditherStrength = ditherStrength,
            .frameIndex = frameIndex,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("dither"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_DITHER_DISPATCH_X - 1) / POST_PROCESS_DITHER_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_DITHER_DISPATCH_Y - 1) / POST_PROCESS_DITHER_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("dither_output");
}
} // Render
