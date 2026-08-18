//
// Created by William on 2026-04-19.
//

#include "post_processing.h"

#include <algorithm>
#include <cmath>

#include "render/render_config.h"
#include "render/vulkan/vk_config.h"
#include "render/interface/render_interface.h"
#include "render/render-graph/render_graph.h"
#include "render/render-graph/render_pass.h"
#include "render/passes/final_gather_passes.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/shaders/constants_interop.h"
#include "render/shaders/push_constant_interop.h"
#include "core/containers/inline_string.h"
#include "render/render_utils.h"

namespace Render
{
static constexpr float EXPOSURE_MIN_LOG_LUMINANCE = -8.0f;
static constexpr float EXPOSURE_MAX_LOG_LUMINANCE = 6.0f;

static uint32_t BloomMipCount(uint32_t width)
{
    return (width >= 3840) ? 6u : 5u;
}

/**
 * CAT02 von Kries white balance: maps (temperature, tint) in [-1, 1] to a luminance-normalized linear-sRGB 3x3 gain matrix.
 * Follows the Unity ColorUtils formulation (offset D65 white point in CIE xy, balance in LMS).
 */
static glm::mat3 ComputeWhiteBalanceMatrix(float temperature, float tint)
{
    auto xyToLMS = [](float x, float y) {
        float Y = 1.0f;
        float X = Y * x / y;
        float Z = Y * (1.0f - x - y) / y;
        return glm::vec3(
            0.7328f * X + 0.4296f * Y - 0.1624f * Z,
            -0.7036f * X + 1.6975f * Y + 0.0061f * Z,
            0.0030f * X + 0.0136f * Y + 0.9834f * Z);
    };

    float x = 0.31271f - temperature * (temperature < 0.0f ? 0.1f : 0.05f);
    float standardIlluminantY = 2.87f * x - 3.0f * x * x - 0.27509507f;
    float y = standardIlluminantY + tint * 0.05f;
    glm::vec3 balance = xyToLMS(0.31271f, 0.32902f) / xyToLMS(x, y);

    const glm::mat3 linToLms(
        0.390405f, 0.0708416f, 0.0231082f,
        0.549941f, 0.963172f, 0.128021f,
        0.00892632f, 0.00135775f, 0.936245f);
    const glm::mat3 lmsToLin(
        2.85847f, -0.210182f, -0.0418120f,
        -1.62879f, 1.15820f, -0.118169f,
        -0.0248910f, 0.000324281f, 1.06867f);

    glm::mat3 balanceMat(1.0f);
    balanceMat[0][0] = balance.x;
    balanceMat[1][1] = balance.y;
    balanceMat[2][2] = balance.z;

    glm::mat3 m = lmsToLin * balanceMat * linToLms;
    glm::vec3 white = m * glm::vec3(1.0f);
    float luma = glm::dot(white, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    return m / std::max(luma, 1e-4f);
}

StringID PPExposure(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bExposureEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    const Core::PostProcessConfiguration& config = ctx.config;
    float deltaTime = ctx.deltaTime;

    // Overlays (text/sprites/debug lines) composite pre-AA into the chain input; meter the clean snapshot when it exists
    StringID meteringSource = input;
    if (graph.HasTexture(SID("lit_color_preoverlay")) && ctx.preAaExtent[0] == width && ctx.preAaExtent[1] == height) {
        meteringSource = SID("lit_color_preoverlay");
    }

    graph.CreateBuffer(SID("luminance_histogram"), POST_PROCESS_LUMINANCE_BUFFER_SIZE, false);

    if (!graph.HasBuffer(SID("luminance_buffer"))) {
        graph.CreateBuffer(SID("luminance_buffer"), sizeof(float), false);
        auto& initPass = graph.AddPass(SID("[Exposure] Init Luminance"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::PostProcessing);
        initPass.WriteTransferBuffer(SID("luminance_buffer"));
        initPass.Execute([target = config.exposureTargetLuminance](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(SID("luminance_buffer")), 0, sizeof(float), &target);
        });
    }
    graph.CarryBufferToNextFrame(SID("luminance_buffer"), SID("luminance_buffer"), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    auto& clearPass = graph.AddPass(SID("[Exposure] Clear Histogram"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::PostProcessing);
    clearPass.WriteTransferBuffer(SID("luminance_histogram"));
    clearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        vkCmdFillBuffer(cmd, graph.GetBufferHandle(SID("luminance_histogram")), 0, VK_WHOLE_SIZE, 0);
    });

    // Meters every other pixel per axis with a per-frame 2x2 phase; statistics are insensitive to the decimation
    const uint32_t gridWidth = (width + 1) / 2;
    const uint32_t gridHeight = (height + 1) / 2;
    const uint32_t phase = static_cast<uint32_t>(ctx.frameNumber) & 3u;

    auto& histogramPass = graph.AddPass(SID("[Exposure] Build Histogram"), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    histogramPass.ReadSampledImage(meteringSource);
    histogramPass.ReadWriteBuffer(SID("luminance_histogram"));
    histogramPass.Execute([width, height, gridWidth, gridHeight, phase, meteringSource, pipelines](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        constexpr float logLuminanceRange = EXPOSURE_MAX_LOG_LUMINANCE - EXPOSURE_MIN_LOG_LUMINANCE;
        HistogramBuildPushConstant pc{
            .hdrImageIndex = graph.GetSampledImageViewDescriptorIndex(meteringSource),
            .histogramBufferAddress = graph.GetBufferAddress(SID("luminance_histogram")),
            .width = width,
            .height = height,
            .minLogLuminance = EXPOSURE_MIN_LOG_LUMINANCE,
            .oneOverLogLuminanceRange = 1.0f / logLuminanceRange,
            .sampleOffset = {phase & 1u, phase >> 1u},
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("exposure_build_histogram"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (gridWidth + POST_PROCESS_LUMINANCE_DISPATCH_X - 1) / POST_PROCESS_LUMINANCE_DISPATCH_X;
        uint32_t yDispatch = (gridHeight + POST_PROCESS_LUMINANCE_DISPATCH_Y - 1) / POST_PROCESS_LUMINANCE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    const float lowPercentile = std::clamp(config.exposureLowPercentile, 0.0f, 0.95f);
    const float highPercentile = std::clamp(config.exposureHighPercentile, lowPercentile + 0.01f, 1.0f);
    const float alphaBrighten = 1.0f - std::exp2(-config.exposureSpeedBrighten * deltaTime);
    const float alphaDarken = 1.0f - std::exp2(-config.exposureSpeedDarken * deltaTime);
    const float minAdaptedLuminance = config.exposureTargetLuminance * std::exp2(-config.exposureMaxGainEV);
    const float maxAdaptedLuminance = config.exposureTargetLuminance * std::exp2(-config.exposureMinGainEV);

    auto& exposurePass = graph.AddPass(SID("[Exposure] Calculate Exposure"), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    exposurePass.ReadBuffer(SID("luminance_histogram"));
    exposurePass.ReadWriteBuffer(SID("luminance_buffer"));
    exposurePass.Execute([gridWidth, gridHeight, pipelines, lowPercentile, highPercentile, alphaBrighten, alphaDarken, minAdaptedLuminance, maxAdaptedLuminance](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        ExposureCalculatePushConstant pc{
            .histogramBufferAddress = graph.GetBufferAddress(SID("luminance_histogram")),
            .luminanceBufferAddress = graph.GetBufferAddress(SID("luminance_buffer")),
            .minLogLuminance = EXPOSURE_MIN_LOG_LUMINANCE,
            .logLuminanceRange = EXPOSURE_MAX_LOG_LUMINANCE - EXPOSURE_MIN_LOG_LUMINANCE,
            .alphaBrighten = alphaBrighten,
            .alphaDarken = alphaDarken,
            .lowPercentile = lowPercentile,
            .highPercentile = highPercentile,
            .minAdaptedLuminance = minAdaptedLuminance,
            .maxAdaptedLuminance = maxAdaptedLuminance,
            .totalPixels = gridWidth * gridHeight,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("exposure_calculate_average"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, 1, 1, 1);
    });

    return input;
}

StringID PPDepthOfField(RenderGraph& graph, PipelineManager* pipelines, const Core::PostProcessConfiguration& config, const RenderTargets& targets, Core::Array<uint32_t, 2> extent, uint64_t frameNumber, StringID input)
{
    if (!config.bDepthOfFieldEnabled) { return input; }
    const uint32_t width = extent[0];
    const uint32_t height = extent[1];
    const uint32_t renderWidth = extent[0];
    const uint32_t renderHeight = extent[1];
    StringID depthStencil = targets.depthCopy;

    const uint32_t halfWidth = std::max(1u, (width + 1) / 2);
    const uint32_t halfHeight = std::max(1u, (height + 1) / 2);
    const float nearRadiusPx = std::max(0.0f, config.dofNearRadiusPx);
    const float farRadiusPx = std::max(0.0f, config.dofFarRadiusPx);
    const float sharpHalfRange = std::max(0.0f, config.dofFocusRange) * 0.5f;
    const float nearTransitionInv = 1.0f / std::max(0.01f, config.dofNearTransition);
    const float farTransitionInv = 1.0f / std::max(0.01f, config.dofFarTransition);

    graph.CreateTexture(SID("dof_color_coc"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, halfWidth, halfHeight, 1}, std::nullopt, true);
    graph.CreateTexture(SID("dof_coc_far_min"), TextureInfo{VK_FORMAT_R16_SFLOAT, halfWidth, halfHeight, 1}, std::nullopt, true);

    RenderPass& cocPass = graph.AddPass(SID("[DoF] CoC Downsample"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    cocPass.ReadBuffer(SID("scene_data"));
    cocPass.ReadSampledImage(input);
    cocPass.ReadSampledImage(depthStencil);
    cocPass.WriteStorageImage(SID("dof_color_coc"));
    cocPass.WriteStorageImage(SID("dof_coc_far_min"));
    cocPass.Execute([width, height, renderWidth, renderHeight, halfWidth, halfHeight, input, depthStencil, pipelines, nearRadiusPx, farRadiusPx, sharpHalfRange, nearTransitionInv, farTransitionInv, focusDistance = config.dofFocusDistance](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DofCocPushConstant pc{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .outputExtent = {halfWidth, halfHeight},
            .inputExtent = {width, height},
            .renderExtent = {renderWidth, renderHeight},
            .sceneColorIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("dof_color_coc")),
            .farMinIndex = graph.GetStorageImageViewDescriptorIndex(SID("dof_coc_far_min")),
            .focusDistance = focusDistance,
            .sharpHalfRange = sharpHalfRange,
            .nearTransitionInv = nearTransitionInv,
            .farTransitionInv = farTransitionInv,
            .nearRadiusPx = nearRadiusPx,
            .farRadiusPx = farRadiusPx,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("dof_coc"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (halfWidth + POST_PROCESS_DOF_DISPATCH_X - 1) / POST_PROCESS_DOF_DISPATCH_X;
        uint32_t yDispatch = (halfHeight + POST_PROCESS_DOF_DISPATCH_Y - 1) / POST_PROCESS_DOF_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    const uint32_t mip1Width = std::max(1u, (halfWidth + 1) / 2);
    const uint32_t mip1Height = std::max(1u, (halfHeight + 1) / 2);
    const uint32_t mip2Width = std::max(1u, (mip1Width + 1) / 2);
    const uint32_t mip2Height = std::max(1u, (mip1Height + 1) / 2);

    graph.CreateTexture(SID("dof_color_coc_mip1"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, mip1Width, mip1Height, 1}, std::nullopt, true);
    graph.CreateTexture(SID("dof_color_coc_mip2"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, mip2Width, mip2Height, 1}, std::nullopt, true);

    struct MipStep { StringID passName; StringID input; StringID output; uint32_t inW, inH, outW, outH; };
    const MipStep mipSteps[2] = {
        {SID("[DoF] Color Mip 1"), SID("dof_color_coc"), SID("dof_color_coc_mip1"), halfWidth, halfHeight, mip1Width, mip1Height},
        {SID("[DoF] Color Mip 2"), SID("dof_color_coc_mip1"), SID("dof_color_coc_mip2"), mip1Width, mip1Height, mip2Width, mip2Height},
    };
    for (const MipStep& step : mipSteps) {
        RenderPass& mipPass = graph.AddPass(step.passName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
        mipPass.ReadSampledImage(step.input);
        mipPass.WriteStorageImage(step.output);
        mipPass.Execute([step, pipelines](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            DofMipPushConstant pc{
                .inputExtent = {step.inW, step.inH},
                .outputExtent = {step.outW, step.outH},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(step.input),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(step.output),
            };

            const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("dof_mip"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            uint32_t xDispatch = (step.outW + POST_PROCESS_DOF_DISPATCH_X - 1) / POST_PROCESS_DOF_DISPATCH_X;
            uint32_t yDispatch = (step.outH + POST_PROCESS_DOF_DISPATCH_Y - 1) / POST_PROCESS_DOF_DISPATCH_Y;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
    }

    const uint32_t tiledX = (halfWidth + POST_PROCESS_DOF_TILE_SIZE - 1) / POST_PROCESS_DOF_TILE_SIZE;
    const uint32_t tiledY = (halfHeight + POST_PROCESS_DOF_TILE_SIZE - 1) / POST_PROCESS_DOF_TILE_SIZE;
    const float maxRadiusHalfResPx = std::max(nearRadiusPx, farRadiusPx) * 0.5f;
    const uint32_t dilationRadius = static_cast<uint32_t>(std::ceil(maxRadiusHalfResPx / static_cast<float>(POST_PROCESS_DOF_TILE_SIZE)));

    graph.CreateTexture(SID("dof_tiled_max"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, tiledX, tiledY, 1}, std::nullopt, true);
    graph.CreateTexture(SID("dof_tiled_neighbor_max"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, tiledX, tiledY, 1}, std::nullopt, true);
    graph.CreateBuffer(SID("dof_dispatch_args"), 3 * sizeof(uint32_t), false);
    graph.CreateBuffer(SID("dof_tile_list"), tiledX * tiledY * sizeof(uint32_t), false);

    RenderPass& argsInitPass = graph.AddPass(SID("[DoF] Args Init"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::PostProcessing);
    argsInitPass.WriteTransferBuffer(SID("dof_dispatch_args"));
    argsInitPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const uint32_t init[3] = {0u, 1u, 1u};
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(SID("dof_dispatch_args")), 0, sizeof(init), init);
    });

    RenderPass& tileMaxPass = graph.AddPass(SID("[DoF] Tile Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    tileMaxPass.ReadSampledImage(SID("dof_color_coc"));
    tileMaxPass.WriteStorageImage(SID("dof_tiled_max"));
    tileMaxPass.Execute([halfWidth, halfHeight, tiledX, tiledY, pipelines](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DofTileMaxPushConstant pc{
            .sourceExtent = {halfWidth, halfHeight},
            .tileExtent = {tiledX, tiledY},
            .cocIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_color_coc")),
            .tileMaxIndex = graph.GetStorageImageViewDescriptorIndex(SID("dof_tiled_max")),
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("dof_tile_max"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, tiledX, tiledY, 1);
    });

    RenderPass& neighborMaxPass = graph.AddPass(SID("[DoF] Neighbor Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    neighborMaxPass.ReadSampledImage(SID("dof_tiled_max"));
    neighborMaxPass.WriteStorageImage(SID("dof_tiled_neighbor_max"));
    neighborMaxPass.ReadWriteBuffer(SID("dof_dispatch_args"));
    neighborMaxPass.ReadWriteBuffer(SID("dof_tile_list"));
    neighborMaxPass.Execute([tiledX, tiledY, pipelines, dilationRadius](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DofNeighborMaxPushConstant pc{
            .tileExtent = {tiledX, tiledY},
            .tileMaxIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_tiled_max")),
            .neighborMaxIndex = graph.GetStorageImageViewDescriptorIndex(SID("dof_tiled_neighbor_max")),
            .dilationRadius = dilationRadius,
            .tileListBuffer = graph.GetBufferAddress(SID("dof_tile_list")),
            .indirectArgsBuffer = graph.GetBufferAddress(SID("dof_dispatch_args")),
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("dof_neighbor_max"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (tiledX + POST_PROCESS_DOF_NEIGHBOR_DISPATCH_X - 1) / POST_PROCESS_DOF_NEIGHBOR_DISPATCH_X;
        uint32_t yDispatch = (tiledY + POST_PROCESS_DOF_NEIGHBOR_DISPATCH_Y - 1) / POST_PROCESS_DOF_NEIGHBOR_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    graph.CreateTexture(SID("dof_near"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, halfWidth, halfHeight, 1}, std::nullopt, true);
    graph.CreateTexture(SID("dof_far"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, halfWidth, halfHeight, 1}, std::nullopt, true);

    // Unlisted tiles are never gathered; alpha 0 is what tells the composite to keep the sharp pixel
    RenderPass& layerClearPass = graph.AddPass(SID("[DoF] Layer Clear"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::PostProcessing);
    layerClearPass.WriteClearImage(SID("dof_near"));
    layerClearPass.WriteClearImage(SID("dof_far"));
    layerClearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkClearColorValue clearValue{};
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(cmd, graph.GetImageHandle(SID("dof_near")), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
        vkCmdClearColorImage(cmd, graph.GetImageHandle(SID("dof_far")), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);
    });

    RenderPass& gatherPass = graph.AddPass(SID("[DoF] Gather"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    gatherPass.ReadSampledImage(SID("dof_color_coc"));
    gatherPass.ReadSampledImage(SID("dof_color_coc_mip1"));
    gatherPass.ReadSampledImage(SID("dof_color_coc_mip2"));
    gatherPass.ReadSampledImage(SID("dof_tiled_neighbor_max"));
    gatherPass.ReadIndirectBuffer(SID("dof_dispatch_args"));
    gatherPass.ReadBuffer(SID("dof_tile_list"));
    gatherPass.WriteStorageImage(SID("dof_near"));
    gatherPass.WriteStorageImage(SID("dof_far"));
    gatherPass.Execute([halfWidth, halfHeight, pipelines, frameIndex = static_cast<uint32_t>(frameNumber)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DofGatherPushConstant pc{
            .tileListBuffer = graph.GetBufferAddress(SID("dof_tile_list")),
            .sourceExtent = {halfWidth, halfHeight},
            .cocIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_color_coc")),
            .mip1Index = graph.GetSampledImageViewDescriptorIndex(SID("dof_color_coc_mip1")),
            .mip2Index = graph.GetSampledImageViewDescriptorIndex(SID("dof_color_coc_mip2")),
            .tileNeighborMaxIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_tiled_neighbor_max")),
            .nearOutputIndex = graph.GetStorageImageViewDescriptorIndex(SID("dof_near")),
            .farOutputIndex = graph.GetStorageImageViewDescriptorIndex(SID("dof_far")),
            .frameIndex = frameIndex,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("dof_gather"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(SID("dof_dispatch_args")), 0);
    });

    graph.CreateTexture(SID("dof_near_filtered"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, halfWidth, halfHeight, 1}, std::nullopt, true);
    graph.CreateTexture(SID("dof_far_filtered"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, halfWidth, halfHeight, 1}, std::nullopt, true);

    RenderPass& layerBlurPass = graph.AddPass(SID("[DoF] Layer Blur"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    layerBlurPass.ReadSampledImage(SID("dof_color_coc"));
    layerBlurPass.ReadSampledImage(SID("dof_near"));
    layerBlurPass.ReadSampledImage(SID("dof_far"));
    layerBlurPass.WriteStorageImage(SID("dof_near_filtered"));
    layerBlurPass.WriteStorageImage(SID("dof_far_filtered"));
    layerBlurPass.Execute([halfWidth, halfHeight, pipelines](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DofLayerBlurPushConstant pc{
            .extent = {halfWidth, halfHeight},
            .cocIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_color_coc")),
            .nearIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_near")),
            .farIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_far")),
            .nearOutputIndex = graph.GetStorageImageViewDescriptorIndex(SID("dof_near_filtered")),
            .farOutputIndex = graph.GetStorageImageViewDescriptorIndex(SID("dof_far_filtered")),
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("dof_layer_blur"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (halfWidth + POST_PROCESS_DOF_DISPATCH_X - 1) / POST_PROCESS_DOF_DISPATCH_X;
        uint32_t yDispatch = (halfHeight + POST_PROCESS_DOF_DISPATCH_Y - 1) / POST_PROCESS_DOF_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    graph.CreateTexture(SID("dof_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, std::nullopt, true);

    RenderPass& compositePass = graph.AddPass(SID("[DoF] Composite"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    compositePass.ReadSampledImage(input);
    compositePass.ReadSampledImage(SID("dof_coc_far_min"));
    compositePass.ReadSampledImage(SID("dof_near_filtered"));
    compositePass.ReadSampledImage(SID("dof_far_filtered"));
    compositePass.WriteStorageImage(SID("dof_output"));
    compositePass.Execute([width, height, input, pipelines](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        DofCompositePushConstant pc{
            .extent = {width, height},
            .sceneColorIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .farMinCocIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_coc_far_min")),
            .nearIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_near_filtered")),
            .farIndex = graph.GetSampledImageViewDescriptorIndex(SID("dof_far_filtered")),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("dof_output")),
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("dof_composite"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_DOF_DISPATCH_X - 1) / POST_PROCESS_DOF_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_DOF_DISPATCH_Y - 1) / POST_PROCESS_DOF_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("dof_output");
}

StringID PPMotionBlur(PostProcessContext& ctx, StringID input)
{
    if (!ctx.config.bMotionBlurEnabled) { return input; }
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    const uint32_t renderWidth = ctx.preAaExtent[0];
    const uint32_t renderHeight = ctx.preAaExtent[1];
    PipelineManager* pipelines = ctx.pipelines;
    StringID velocity = ctx.targets.gbufferOne;
    StringID depthStencil = ctx.targets.depthCopy;
    float depthScale = ctx.config.motionBlurDepthScale;

    float velocityScale = ctx.config.motionBlurVelocityScale;
    if (ctx.config.motionBlurTargetFps > 0.0f && ctx.deltaTime > 1e-6f) {
        velocityScale /= ctx.deltaTime * ctx.config.motionBlurTargetFps;
    }
    velocityScale *= 0.5f;

    const float maxRadiusPx = std::max(1.0f, ctx.config.motionBlurMaxRadiusPx);
    const uint32_t dilationRadius = static_cast<uint32_t>(std::ceil(maxRadiusPx / static_cast<float>(POST_PROCESS_MOTION_BLUR_TILE_SIZE)));

    const bool bObjectOnly = ctx.config.bMotionBlurObjectOnly;
    if (bObjectOnly) {
        SetupObjectMotion(graph, pipelines, ctx.preAaExtent, ctx.targets, 0);
    }

    graph.CreateTexture(SID("motion_blur_velocity"), TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1}, std::nullopt, true);
    RenderPass& velocityExtractPass = graph.AddPass(SID("[Motion Blur] Velocity Extract"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    velocityExtractPass.ReadBuffer(SID("scene_data"));
    if (bObjectOnly) {
        velocityExtractPass.ReadSampledImage(OBJECT_MOTION);
    } else {
        velocityExtractPass.ReadSampledImage(velocity);
        velocityExtractPass.ReadSampledImage(depthStencil);
    }
    velocityExtractPass.WriteStorageImage(SID("motion_blur_velocity"));
    velocityExtractPass.Execute([width, height, renderWidth, renderHeight, pipelines, velocity, depthStencil, bObjectOnly](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        MotionBlurVelocityExtractPushConstant pc{
            .sceneData = graph.GetBufferAddress(SID("scene_data")),
            .extent = {width, height},
            .renderExtent = {renderWidth, renderHeight},
            .gbufferOneIndex = bObjectOnly ? ~0u : graph.GetSampledImageViewDescriptorIndex(velocity),
            .depthBufferIndex = bObjectOnly ? ~0u : graph.GetSampledImageViewDescriptorIndex(depthStencil),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_velocity")),
            .bObjectOnly = bObjectOnly ? 1u : 0u,
            .objectMotionIndex = bObjectOnly ? graph.GetSampledImageViewDescriptorIndex(OBJECT_MOTION) : ~0u,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("motion_blur_velocity_extract"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_MOTION_BLUR_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_MOTION_BLUR_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });
    velocity = SID("motion_blur_velocity");

    uint32_t blurTiledX = (width + POST_PROCESS_MOTION_BLUR_TILE_SIZE - 1) / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
    uint32_t blurTiledY = (height + POST_PROCESS_MOTION_BLUR_TILE_SIZE - 1) / POST_PROCESS_MOTION_BLUR_TILE_SIZE;
    graph.CreateTexture(SID("motion_blur_tiled_max"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, blurTiledX, blurTiledY, 1}, std::nullopt, true);
    graph.CreateTexture(SID("motion_blur_tiled_neighbor_max"), TextureInfo{VK_FORMAT_R16G16_SFLOAT, blurTiledX, blurTiledY, 1}, std::nullopt, true);
    graph.CreateTexture(SID("motion_blur_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, std::nullopt, true);
    graph.CreateBuffer(SID("motion_blur_dispatch_args"), 3 * sizeof(uint32_t), false);
    graph.CreateBuffer(SID("motion_blur_tile_list"), blurTiledX * blurTiledY * sizeof(uint32_t), false);

    RenderPass& argsInitPass = graph.AddPass(SID("[Motion Blur] Args Init"), VK_PIPELINE_STAGE_2_CLEAR_BIT, Render::RenderCategory::PostProcessing);
    argsInitPass.WriteTransferBuffer(SID("motion_blur_dispatch_args"));
    argsInitPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const uint32_t init[3] = {0u, 1u, 1u};
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(SID("motion_blur_dispatch_args")), 0, sizeof(init), init);
    });

    RenderPass& motionBlurTiledMaxPass = graph.AddPass(SID("[Motion Blur] Tiled Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    motionBlurTiledMaxPass.ReadSampledImage(velocity);
    motionBlurTiledMaxPass.WriteStorageImage(SID("motion_blur_tiled_max"));
    motionBlurTiledMaxPass.Execute([width, height, blurTiledX, blurTiledY, pipelines, velocity, velocityScale, maxRadiusPx](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        MotionBlurTileVelocityPushConstant pc{
            .velocityBufferSize = {width, height},
            .tileBufferSize = {blurTiledX, blurTiledY},
            .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex(velocity),
            .tileMaxIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_tiled_max")),
            .velocityScale = velocityScale,
            .maxRadiusPx = maxRadiusPx,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("motion_blur_tile_max"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, blurTiledX, blurTiledY, 1);
    });

    RenderPass& motionBlurNeighborMax = graph.AddPass(SID("[Motion Blur] Neighbor Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    motionBlurNeighborMax.ReadSampledImage(SID("motion_blur_tiled_max"));
    motionBlurNeighborMax.WriteStorageImage(SID("motion_blur_tiled_neighbor_max"));
    motionBlurNeighborMax.ReadWriteBuffer(SID("motion_blur_dispatch_args"));
    motionBlurNeighborMax.ReadWriteBuffer(SID("motion_blur_tile_list"));
    motionBlurNeighborMax.Execute([width, height, blurTiledX, blurTiledY, pipelines, dilationRadius](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        MotionBlurNeighborMaxPushConstant pc{
            .tileBufferSize = {blurTiledX, blurTiledY},
            .tileMaxIndex = graph.GetSampledImageViewDescriptorIndex(SID("motion_blur_tiled_max")),
            .neighborMaxIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_tiled_neighbor_max")),
            .dilationRadius = dilationRadius,
            .tileListBuffer = graph.GetBufferAddress(SID("motion_blur_tile_list")),
            .indirectArgsBuffer = graph.GetBufferAddress(SID("motion_blur_dispatch_args")),
            .velocityBufferSize = {width, height},
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("motion_blur_neighbor_max"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (blurTiledX + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X;
        uint32_t yDispatch = (blurTiledY + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    // Static tiles keep this copy; the indirect reconstruction only touches listed tiles
    RenderPass& prefillPass = graph.AddPass(SID("[Motion Blur] Output Prefill"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::RenderCategory::PostProcessing);
    prefillPass.ReadCopyImage(input);
    prefillPass.WriteCopyImage(SID("motion_blur_output"));
    prefillPass.Execute([width, height, input](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        VkImageCopy2 copyRegion{};
        copyRegion.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.extent = {width, height, 1};

        VkCopyImageInfo2 copyInfo{};
        copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
        copyInfo.srcImage = graph.GetImageHandle(input);
        copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyInfo.dstImage = graph.GetImageHandle(SID("motion_blur_output"));
        copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &copyRegion;
        vkCmdCopyImage2(cmd, &copyInfo);
    });

    RenderPass& motionBlurReconstructionPass = graph.AddPass(SID("[Motion Blur] Reconstruction"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    motionBlurReconstructionPass.ReadSampledImage(input);
    motionBlurReconstructionPass.ReadSampledImage(velocity);
    motionBlurReconstructionPass.ReadSampledImage(SID("motion_blur_tiled_neighbor_max"));
    motionBlurReconstructionPass.ReadIndirectBuffer(SID("motion_blur_dispatch_args"));
    motionBlurReconstructionPass.ReadBuffer(SID("motion_blur_tile_list"));
    motionBlurReconstructionPass.WriteStorageImage(SID("motion_blur_output"));
    motionBlurReconstructionPass.Execute([width, height, input, pipelines, velocity, velocityScale, depthScale, maxRadiusPx](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        MotionBlurReconstructionPushConstant pc{
            .tileListBuffer = graph.GetBufferAddress(SID("motion_blur_tile_list")),
            .srcBufferSize = {width, height},
            .sceneColorIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .velocityBufferIndex = graph.GetSampledImageViewDescriptorIndex(velocity),
            .tileNeighborMaxIndex = graph.GetSampledImageViewDescriptorIndex(SID("motion_blur_tiled_neighbor_max")),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("motion_blur_output")),
            .velocityScale = velocityScale,
            .depthScale = depthScale,
            .maxRadiusPx = maxRadiusPx,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("motion_blur_reconstruction"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(SID("motion_blur_dispatch_args")), 0);
    });

    return SID("motion_blur_output");
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
    float targetLuminance = ctx.config.exposureTargetLuminance;
    bool bExposureEnabled = ctx.config.bExposureEnabled;

    // Chain lives at half res; mip 0 is the fused threshold + first downsample
    const uint32_t halfWidth = std::max(1u, width / 2);
    const uint32_t halfHeight = std::max(1u, height / 2);
    const uint32_t numMips = BloomMipCount(width);
    graph.CreateTexture(SID("bloom_chain"), TextureInfo{COLOR_ATTACHMENT_FORMAT, halfWidth, halfHeight, numMips}, std::nullopt, true);

    RenderPass& thresholdPass = graph.AddPass(SID("[Bloom] Threshold"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    thresholdPass.ReadSampledImage(input);
    if (bExposureEnabled) { thresholdPass.ReadBuffer(SID("luminance_buffer")); }
    thresholdPass.ReadWriteImage(SID("bloom_chain"));
    thresholdPass.Execute([width, height, halfWidth, halfHeight, input, pipelines, bloomThreshold, bloomSoftThreshold, bloomClamp, targetLuminance, bExposureEnabled](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        BloomThresholdPushConstant pc{
            .outputExtent = {halfWidth, halfHeight},
            .inputExtent = {width, height},
            .inputColorIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("bloom_chain"), 0),
            .luminanceBufferAddress = bExposureEnabled ? graph.GetBufferAddress(SID("luminance_buffer")) : 0,
            .threshold = bloomThreshold,
            .softThreshold = bloomSoftThreshold,
            .clampValue = bloomClamp,
            .targetLuminance = targetLuminance,
            .bExposureEnabled = bExposureEnabled ? 1u : 0u,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("bloom_threshold"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (halfWidth + POST_PROCESS_BLOOM_DISPATCH_X - 1) / POST_PROCESS_BLOOM_DISPATCH_X;
        uint32_t yDispatch = (halfHeight + POST_PROCESS_BLOOM_DISPATCH_Y - 1) / POST_PROCESS_BLOOM_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    for (uint32_t i = 0; i + 1 < numMips; ++i) {
        uint32_t srcWidth = std::max(1u, halfWidth >> i);
        uint32_t srcHeight = std::max(1u, halfHeight >> i);
        uint32_t mipWidth = std::max(1u, halfWidth >> (i + 1));
        uint32_t mipHeight = std::max(1u, halfHeight >> (i + 1));

        Core::InlineString<32> passName = Core::InlineString<32>::Format("[Bloom] Downsample %u", i);
        RenderPass& downsamplePass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
        downsamplePass.ReadWriteImage(SID("bloom_chain"));
        downsamplePass.Execute([srcWidth, srcHeight, mipWidth, mipHeight, srcMip = i, dstMip = i + 1, pipelines](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            BloomDownsamplePushConstant pc{
                .outputExtent = {mipWidth, mipHeight},
                .inputExtent = {srcWidth, srcHeight},
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

    for (int32_t i = static_cast<int32_t>(numMips) - 2; i >= 0; --i) {
        uint32_t mipWidth = std::max(1u, halfWidth >> i);
        uint32_t mipHeight = std::max(1u, halfHeight >> i);
        uint32_t lowerWidth = std::max(1u, halfWidth >> (i + 1));
        uint32_t lowerHeight = std::max(1u, halfHeight >> (i + 1));

        Core::InlineString<32> passName = Core::InlineString<32>::Format("[Bloom] Upsample %d", i);
        RenderPass& upsamplePass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
        upsamplePass.ReadWriteImage(SID("bloom_chain"));
        upsamplePass.Execute([mipWidth, mipHeight, lowerWidth, lowerHeight, dstMip = i, lowerMip = i + 1, pipelines, bloomRadius](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            BloomUpsamplePushConstant pc{
                .outputExtent = {mipWidth, mipHeight},
                .lowerExtent = {lowerWidth, lowerHeight},
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

StringID PPFinalize(PostProcessContext& ctx, StringID input)
{
    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    const Core::PostProcessConfiguration& config = ctx.config;

    const bool bBloomEnabled = config.bBloomEnabled;
    const bool bExposureEnabled = config.bExposureEnabled;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);

    PostProcessFinalizePushConstant constants{};
    constants.outputExtent = {width, height};
    constants.tonemapOperator = config.tonemapOperator;
    constants.targetLuminance = config.exposureTargetLuminance;
    constants.exposureBias = config.bColorGradingEnabled ? std::exp2(config.colorGradingExposure) : 1.0f;
    constants.bloomIntensity = config.bloomIntensity / static_cast<float>(BloomMipCount(width));
    constants.aspect = aspect;

    switch (config.tonemapOperator) {
        case 1: constants.params0.x = config.hableParams.whitePoint; break;
        case 2: constants.params0.x = config.reinhardParams.whitePoint; break;
        case 7:
            constants.params0 = glm::vec4(config.uchimuraParams.P, config.uchimuraParams.a, config.uchimuraParams.m, config.uchimuraParams.l);
            constants.params1 = glm::vec4(config.uchimuraParams.c, config.uchimuraParams.b, 0.0f, 0.0f);
            break;
        case 9: constants.params0 = glm::vec4(config.agxParams.minEV, config.agxParams.maxEV, 0.0f, 0.0f); break;
        case 10: constants.params0 = glm::vec4(config.khronosParams.startCompression, config.khronosParams.desaturation, 0.0f, 0.0f); break;
        default: break;
    }

    const bool bGradingActive = config.bColorGradingEnabled &&
        (config.colorGradingContrast != 1.0f || config.colorGradingSaturation != 1.0f || config.colorGradingTemperature != 0.0f || config.colorGradingTint != 0.0f);
    glm::mat3 wb(1.0f);
    if (bGradingActive && (config.colorGradingTemperature != 0.0f || config.colorGradingTint != 0.0f)) {
        wb = ComputeWhiteBalanceMatrix(config.colorGradingTemperature, config.colorGradingTint);
    }
    constants.wbRow0 = glm::vec4(wb[0][0], wb[1][0], wb[2][0], 0.0f);
    constants.wbRow1 = glm::vec4(wb[0][1], wb[1][1], wb[2][1], 0.0f);
    constants.wbRow2 = glm::vec4(wb[0][2], wb[1][2], wb[2][2], 0.0f);
    constants.contrast = config.colorGradingContrast;
    constants.saturation = config.colorGradingSaturation;

    constants.vignetteStrength = config.bVignetteEnabled ? config.vignetteStrength : 0.0f;
    constants.vignetteRadius = config.vignetteRadius;
    constants.vignetteSmoothness = std::max(config.vignetteSmoothness, 1e-4f);
    constants.vignetteRoundness = std::clamp(config.vignetteRoundness, 0.0f, 1.0f);
    constants.chromaticAberrationStrength = config.bChromaticAberrationEnabled ? std::max(config.chromaticAberrationStrength, 0.0f) : 0.0f;

    const float paniniD = config.bPaniniEnabled ? std::clamp(config.paniniStrength, 0.0f, 1.0f) : 0.0f;
    constants.paniniStrength = paniniD;
    if (paniniD > 0.0f) {
        float fov = ctx.view.mainView.currentViewData.fovRadians;
        float horizontalFov = 2.0f * std::atan(std::tan(fov * 0.5f) * aspect);
        float f = std::tan(horizontalFov * 0.5f);
        constants.paniniB = f * (paniniD + 1.0f) / (paniniD * std::sqrt(1.0f + f * f) + 1.0f);
        constants.paniniVerticalFocalLength = 1.0f / std::tan(fov * 0.5f);
    }

    constants.flags = (bExposureEnabled ? POST_PROCESS_FINALIZE_FLAG_EXPOSURE : 0u) |
                      (bBloomEnabled ? POST_PROCESS_FINALIZE_FLAG_BLOOM : 0u) |
                      (bGradingActive ? POST_PROCESS_FINALIZE_FLAG_GRADING : 0u);

    graph.CreateTexture(SID("tonemap_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, std::nullopt, true);
    RenderPass& finalizePass = graph.AddPass(SID("[Finalize] Tonemap + Grade + Lens"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    finalizePass.ReadSampledImage(input);
    if (bBloomEnabled) { finalizePass.ReadSampledImage(SID("bloom_chain")); }
    if (bExposureEnabled) { finalizePass.ReadBuffer(SID("luminance_buffer")); }
    finalizePass.WriteStorageImage(SID("tonemap_output"));
    finalizePass.Execute([constants, input, pipelines, bBloomEnabled, bExposureEnabled](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        PostProcessFinalizePushConstant pc = constants;
        pc.srcImageIndex = graph.GetSampledImageViewDescriptorIndex(input);
        pc.dstImageIndex = graph.GetStorageImageViewDescriptorIndex(SID("tonemap_output"));
        pc.bloomImageIndex = bBloomEnabled ? graph.GetSampledImageViewDescriptorIndex(SID("bloom_chain")) : 0u;
        pc.luminanceBufferAddress = bExposureEnabled ? graph.GetBufferAddress(SID("luminance_buffer")) : 0;

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("post_process_finalize"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (pc.outputExtent.x + POST_PROCESS_FINALIZE_DISPATCH_X - 1) / POST_PROCESS_FINALIZE_DISPATCH_X;
        uint32_t yDispatch = (pc.outputExtent.y + POST_PROCESS_FINALIZE_DISPATCH_Y - 1) / POST_PROCESS_FINALIZE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("tonemap_output");
}

StringID PPCompose(PostProcessContext& ctx, StringID input)
{
    const Core::PostProcessConfiguration& config = ctx.config;
    const float sharpenStrength = config.bSharpeningEnabled ? std::max(config.sharpeningStrength, 0.0f) : 0.0f;
    const float grainStrength = config.bFilmGrainEnabled ? std::max(config.grainStrength, 0.0f) : 0.0f;
    const bool bDisplayResolution = ctx.extent[0] == ctx.displayExtent[0] && ctx.extent[1] == ctx.displayExtent[1];
    const float ditherStrength = (config.bDitherEnabled && bDisplayResolution) ? std::max(config.ditherStrength, 0.0f) : 0.0f;
    if (sharpenStrength <= 0.0f && grainStrength <= 0.0f && ditherStrength <= 0.0f) { return input; }

    RenderGraph& graph = ctx.graph;
    const uint32_t width = ctx.extent[0];
    const uint32_t height = ctx.extent[1];
    PipelineManager* pipelines = ctx.pipelines;
    const float grainSize = std::max(config.grainSize, 1.0f);
    const float grainResponse = std::clamp(config.grainResponse, 0.0f, 1.0f);
    const uint32_t frameIndex = static_cast<uint32_t>(ctx.frameNumber);

    graph.CreateTexture(SID("post_process_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, std::nullopt, true);
    RenderPass& composePass = graph.AddPass(SID("[Compose] Sharpen + Grain + Dither"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    composePass.ReadSampledImage(input);
    composePass.WriteStorageImage(SID("post_process_output"));
    composePass.Execute([width, height, input, pipelines, sharpenStrength, grainStrength, grainSize, grainResponse, ditherStrength, frameIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        PostProcessComposePushConstant pc{
            .outputExtent = {width, height},
            .inputIndex = graph.GetSampledImageViewDescriptorIndex(input),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("post_process_output")),
            .sharpenStrength = sharpenStrength,
            .grainStrength = grainStrength,
            .grainSize = grainSize,
            .grainResponse = grainResponse,
            .ditherStrength = ditherStrength,
            .frameIndex = frameIndex,
        };

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("post_process_compose"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (width + POST_PROCESS_COMPOSE_DISPATCH_X - 1) / POST_PROCESS_COMPOSE_DISPATCH_X;
        uint32_t yDispatch = (height + POST_PROCESS_COMPOSE_DISPATCH_Y - 1) / POST_PROCESS_COMPOSE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("post_process_output");
}

StringID PPScreenFade(RenderGraph& graph, PipelineManager* pipelines, const Core::ScreenFadeState& fade, Core::Array<uint32_t, 2> extent, StringID input)
{
    if (fade.mode == Core::ScreenFadeMode::None || fade.progress <= 0.0f) { return input; }

    const uint32_t width = extent[0];
    const uint32_t height = extent[1];

    ScreenFadePushConstant constants{};
    constants.outputExtent = {width, height};
    constants.center = fade.center;
    constants.direction = fade.direction;
    constants.color = glm::vec4(fade.color, 1.0f);
    constants.progress = std::clamp(fade.progress, 0.0f, 1.0f);
    constants.softness = std::max(fade.softness, 0.0f);
    constants.aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1u));
    constants.mode = static_cast<uint32_t>(fade.mode);

    graph.CreateTexture(SID("screen_fade_output"), TextureInfo{COLOR_ATTACHMENT_FORMAT, width, height, 1}, std::nullopt, true);
    RenderPass& fadePass = graph.AddPass(SID("[Screen Fade] Overlay"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::PostProcessing);
    fadePass.ReadSampledImage(input);
    fadePass.WriteStorageImage(SID("screen_fade_output"));
    fadePass.Execute([constants, input, pipelines](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        ScreenFadePushConstant pc = constants;
        pc.inputIndex = graph.GetSampledImageViewDescriptorIndex(input);
        pc.outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("screen_fade_output"));

        const PipelineEntry* pipelineEntry = pipelines->GetPipelineEntry(SID("screen_fade"));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        uint32_t xDispatch = (pc.outputExtent.x + SCREEN_FADE_DISPATCH_X - 1) / SCREEN_FADE_DISPATCH_X;
        uint32_t yDispatch = (pc.outputExtent.y + SCREEN_FADE_DISPATCH_Y - 1) / SCREEN_FADE_DISPATCH_Y;
        vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
    });

    return SID("screen_fade_output");
}
} // Render
