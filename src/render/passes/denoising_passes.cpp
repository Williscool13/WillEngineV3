//
// Created by William on 2026-06-03.
//

#include "render/passes/denoising_passes.h"

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
void SetupATrousWaveletDenoiser(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                Core::Array<uint32_t, 2> renderExtent,
                                const DeferredResolveTargets& targets,
                                const Core::ReSTIRParams::ATrousParams& params)
{
    const StringID gbufferOne = targets.gbufferOne;
    const StringID depthStencil = targets.depthStencil;
    const StringID lightingOutput = targets.output;

    constexpr int32_t ATROUS_PASS_COUNT = 4;
    const int32_t ATROUS_ITERATIONS = params.iterations;

    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];
    const TextureInfo texInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1};

    graph.CreateTexture(SID("atrous_0"), texInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("atrous_1"), texInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("atrous_2"), texInfo, {std::nullopt}, true);

    const StringID inputs[ATROUS_PASS_COUNT] = {lightingOutput, SID("atrous_0"), SID("atrous_1"), SID("atrous_2")};
    const StringID outputs[ATROUS_PASS_COUNT] = {SID("atrous_0"), SID("atrous_1"), SID("atrous_2"), lightingOutput};
    const char* passNames[ATROUS_PASS_COUNT] = {"[ATrous] Iteration 0", "[ATrous] Iteration 1", "[ATrous] Iteration 2", "[ATrous] Iteration 3"};

    for (int32_t i = 0; i < ATROUS_ITERATIONS; i++) {
        const bool isLast = (i == ATROUS_ITERATIONS - 1);
        const StringID inputTex = inputs[i];
        const StringID outputTex = isLast ? outputs[ATROUS_PASS_COUNT - 1] : outputs[i];
        const uint32_t stepSize = 1u << static_cast<uint32_t>(i);

        auto& pass = graph.AddPass(SID(passNames[i]), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Denoising);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depthStencil);
        if (inputTex == outputTex) {
            pass.ReadWriteImage(inputTex);
        }
        else {
            pass.ReadSampledImage(inputTex);
            pass.WriteStorageImage(outputTex);
        }
        pass.Execute([&graph, pipelineManager, inputTex, outputTex, gbufferOne, depthStencil,
                width, height, stepSize, params](VkCommandBuffer cmd) {
                ATrousWaveletPushConstant pc{
                    .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                    .inputColorIndex = graph.GetSampledImageViewDescriptorIndex(inputTex),
                    .outputColorIndex = graph.GetStorageImageViewDescriptorIndex(outputTex),
                    .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depthStencil),
                    .stepSize = stepSize,
                    .width = width,
                    .height = height,
                    .sigmaLuminance = params.sigmaLuminance,
                    .sigmaNormal = params.sigmaNormal,
                    .sigmaDepth = params.sigmaDepth,
                };
                const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("atrous_wavelet"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
                vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                const uint32_t groupsX = (width + 7) / 8;
                const uint32_t groupsY = (height + 7) / 8;
                vkCmdDispatch(cmd, groupsX, groupsY, 1);
            });
    }
}

void SetupASVGFDenoiser(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        Core::Array<uint32_t, 2> renderExtent,
                        const DeferredResolveTargets& targets,
                        const Core::ReSTIRParams::SVGFParams& params)
{
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];
    constexpr uint32_t GRADIENT_STRIDE = 3u;
    const uint32_t gradW = (width + GRADIENT_STRIDE - 1) / GRADIENT_STRIDE;
    const uint32_t gradH = (height + GRADIENT_STRIDE - 1) / GRADIENT_STRIDE;

    const StringID gbufferOne = targets.gbufferOne;
    const StringID gbufferTwo = targets.gbufferTwo;
    const StringID depth = targets.depthStencil;
    const StringID lightingOutput = targets.output;

    const TextureInfo colorHistInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1};
    const TextureInfo histLenInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};

    graph.CreateTexture(SID("svgf_color_accum"), colorHistInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_moments"), TextureInfo{VK_FORMAT_R32G32_SFLOAT, width, height, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_history_length"), histLenInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_variance"), TextureInfo{VK_FORMAT_R16_SFLOAT, width, height, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_gradient_samples"), TextureInfo{VK_FORMAT_R16_SFLOAT, gradW, gradH, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_gradient"), TextureInfo{VK_FORMAT_R16_SFLOAT, gradW, gradH, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_gradient_full"), TextureInfo{VK_FORMAT_R16_SFLOAT, width, height, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_gradient_spread_0"), TextureInfo{VK_FORMAT_R16_SFLOAT, width, height, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_atrous_0"), colorHistInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_atrous_1"), colorHistInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_atrous_2"), colorHistInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_denoised"), colorHistInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_atrous_variance_0"), TextureInfo{VK_FORMAT_R16_SFLOAT, width, height, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_atrous_variance_1"), TextureInfo{VK_FORMAT_R16_SFLOAT, width, height, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_atrous_variance_2"), TextureInfo{VK_FORMAT_R16_SFLOAT, width, height, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_atrous_variance_last"), TextureInfo{VK_FORMAT_R32_SFLOAT, width, height, 1}, {std::nullopt}, true);
    graph.CreateTexture(SID("svgf_variance_estimated"), TextureInfo{VK_FORMAT_R16_SFLOAT, width, height, 1}, {std::nullopt}, true);

    graph.CarryTextureToNextFrame(SID("svgf_color_accum"), SID("svgf_color_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("svgf_moments"), SID("svgf_moments_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("svgf_history_length"), SID("svgf_history_length_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("svgf_gradient"), SID("svgf_gradient_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

    // Pass 1: Gradient Samples
    {
        auto& pass = graph.AddPass(SID("[SVGF] Gradient Samples"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Denoising);
        pass.ReadSampledImage(lightingOutput);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        if (graph.HasTexture(SID("svgf_color_history"))) {
            pass.ReadSampledImage(SID("svgf_color_history"));
        }
        pass.WriteStorageImage(SID("svgf_gradient_samples"));
        pass.Execute([&graph, pipelineManager, lightingOutput, gbufferOne, depth, width, height](VkCommandBuffer cmd) {
            const bool hasHistory = graph.HasTexture(SID("svgf_color_history"));
            SVGFGradientSamplesPushConstant pc{
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(lightingOutput),
                .colorHistoryIndex = hasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("svgf_color_history")) : graph.GetSampledImageViewDescriptorIndex(lightingOutput),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .outputGradientIndex = graph.GetStorageImageViewDescriptorIndex(SID("svgf_gradient_samples")),
                .width = width,
                .height = height,
                .stride = GRADIENT_STRIDE,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("svgf_gradient_samples"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            const uint32_t gx = ((width + GRADIENT_STRIDE - 1) / GRADIENT_STRIDE + 7) / 8;
            const uint32_t gy = ((height + GRADIENT_STRIDE - 1) / GRADIENT_STRIDE + 7) / 8;
            vkCmdDispatch(cmd, gx, gy, 1);
        });
    }

    // Pass 2: Gradient Temporal Filter
    {
        auto& pass = graph.AddPass(SID("[SVGF] Gradient Temporal"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Denoising);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.ReadSampledImage(SID("svgf_gradient_samples"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        if (graph.HasTexture(SID("svgf_gradient_history"))) {
            pass.ReadSampledImage(SID("svgf_gradient_history"));
        }
        if (graph.HasTexture(SID("gbuffer_one_history"))) {
            pass.ReadSampledImage(SID("gbuffer_one_history"));
        }
        if (graph.HasTexture(SID("depth_history"))) {
            pass.ReadSampledImage(SID("depth_history"));
        }
        pass.WriteStorageImage(SID("svgf_gradient"));
        pass.Execute([&graph, pipelineManager, gbufferOne, depth, width, height](VkCommandBuffer cmd) {
            const bool hasHistory = graph.HasTexture(SID("svgf_gradient_history"));
            SVGFGradientTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .gradientSamplesIndex = graph.GetSampledImageViewDescriptorIndex(SID("svgf_gradient_samples")),
                .gradientHistoryIndex = hasHistory ? graph.GetSampledImageViewDescriptorIndex(SID("svgf_gradient_history")) : graph.GetSampledImageViewDescriptorIndex(SID("svgf_gradient_samples")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferOneHistoryIndex = graph.HasTexture(SID("gbuffer_one_history")) ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .depthHistoryIndex = graph.HasTexture(SID("depth_history")) ? graph.GetDepthOnlySampledImageViewDescriptorIndex(SID("depth_history")) : graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .outputGradientIndex = graph.GetStorageImageViewDescriptorIndex(SID("svgf_gradient")),
                .width = width,
                .height = height,
                .stride = GRADIENT_STRIDE,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("svgf_gradient_temporal"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            const uint32_t gx = ((width + GRADIENT_STRIDE - 1) / GRADIENT_STRIDE + 7) / 8;
            const uint32_t gy = ((height + GRADIENT_STRIDE - 1) / GRADIENT_STRIDE + 7) / 8;
            vkCmdDispatch(cmd, gx, gy, 1);
        });
    }

    // Pass 3: Gradient Upsample + Spread
    {
        auto& pass = graph.AddPass(SID("[SVGF] Gradient Upsample"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Denoising);
        pass.ReadSampledImage(SID("svgf_gradient"));
        pass.WriteStorageImage(SID("svgf_gradient_full"));
        pass.Execute([&graph, pipelineManager, width, height](VkCommandBuffer cmd) {
            SVGFGradientAtrousPushConstant pc{
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("svgf_gradient")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("svgf_gradient_full")),
                .width = width,
                .height = height,
                .stepSize = GRADIENT_STRIDE,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("svgf_gradient_atrous"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }
    {
        auto& pass = graph.AddPass(SID("[SVGF] Gradient Spread"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Denoising);
        pass.ReadSampledImage(SID("svgf_gradient_full"));
        pass.WriteStorageImage(SID("svgf_gradient_spread_0"));
        pass.Execute([&graph, pipelineManager, width, height](VkCommandBuffer cmd) {
            SVGFGradientAtrousPushConstant pc{
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(SID("svgf_gradient_full")),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(SID("svgf_gradient_spread_0")),
                .width = width,
                .height = height,
                .stepSize = 2u,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("svgf_gradient_atrous"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 4: Temporal Accumulation
    {
        auto& pass = graph.AddPass(SID("[SVGF] Temporal Accumulation"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Denoising);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.ReadSampledImage(lightingOutput);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(SID("svgf_gradient_spread_0"));
        if (graph.HasTexture(SID("svgf_color_history"))) {
            pass.ReadSampledImage(SID("svgf_color_history"));
        }
        if (graph.HasTexture(SID("svgf_moments_history"))) {
            pass.ReadSampledImage(SID("svgf_moments_history"));
        }
        if (graph.HasTexture(SID("svgf_history_length_history"))) {
            pass.ReadSampledImage(SID("svgf_history_length_history"));
        }
        if (graph.HasTexture(SID("gbuffer_one_history"))) {
            pass.ReadSampledImage(SID("gbuffer_one_history"));
        }
        if (graph.HasTexture(SID("depth_history"))) {
            pass.ReadSampledImage(SID("depth_history"));
        }
        pass.WriteStorageImage(SID("svgf_color_accum"));
        pass.WriteStorageImage(SID("svgf_moments"));
        pass.WriteStorageImage(SID("svgf_history_length"));
        pass.WriteStorageImage(SID("svgf_variance"));
        pass.Execute([&graph, pipelineManager, lightingOutput, gbufferOne, depth, width, height, params](VkCommandBuffer cmd) {
            const bool hasColorHist = graph.HasTexture(SID("svgf_color_history"));
            const bool hasMomentsHist = graph.HasTexture(SID("svgf_moments_history"));
            const bool hasLenHist = graph.HasTexture(SID("svgf_history_length_history"));
            SVGFTemporalAccumulationPushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .colorIndex = graph.GetSampledImageViewDescriptorIndex(lightingOutput),
                .colorHistoryIndex = hasColorHist ? graph.GetSampledImageViewDescriptorIndex(SID("svgf_color_history")) : graph.GetSampledImageViewDescriptorIndex(lightingOutput),
                .momentsHistoryIndex = hasMomentsHist ? graph.GetSampledImageViewDescriptorIndex(SID("svgf_moments_history")) : graph.GetSampledImageViewDescriptorIndex(SID("svgf_moments")),
                .historyLengthIndex = hasLenHist ? graph.GetSampledImageViewDescriptorIndex(SID("svgf_history_length_history")) : graph.GetSampledImageViewDescriptorIndex(SID("svgf_history_length")),
                .gradientIndex = graph.GetSampledImageViewDescriptorIndex(SID("svgf_gradient_spread_0")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferOneHistoryIndex = graph.HasTexture(SID("gbuffer_one_history")) ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .depthHistoryIndex = graph.HasTexture(SID("depth_history")) ? graph.GetDepthOnlySampledImageViewDescriptorIndex(SID("depth_history")) : graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .outputColorIndex = graph.GetStorageImageViewDescriptorIndex(SID("svgf_color_accum")),
                .outputMomentsIndex = graph.GetStorageImageViewDescriptorIndex(SID("svgf_moments")),
                .outputHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(SID("svgf_history_length")),
                .outputVarianceIndex = graph.GetStorageImageViewDescriptorIndex(SID("svgf_variance")),
                .width = width,
                .height = height,
                .alphaMin = params.alphaMin,
                .gradientThreshold = params.gradientThreshold,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("svgf_temporal_accumulation"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 5: Spatial Variance Estimate
    if (params.atrousIterations > 0) {
        auto& pass = graph.AddPass(SID("[SVGF] Variance Estimate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Denoising);
        pass.ReadSampledImage(SID("svgf_moments"));
        pass.ReadSampledImage(SID("svgf_history_length"));
        pass.ReadSampledImage(SID("svgf_variance"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.WriteStorageImage(SID("svgf_variance_estimated"));
        pass.Execute([&graph, pipelineManager, gbufferOne, depth, width, height](VkCommandBuffer cmd) {
            SVGFVarianceEstimatePushConstant pc{
                .momentsIndex = graph.GetSampledImageViewDescriptorIndex(SID("svgf_moments")),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("svgf_history_length")),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .inputVarianceIndex = graph.GetSampledImageViewDescriptorIndex(SID("svgf_variance")),
                .outputVarianceIndex = graph.GetStorageImageViewDescriptorIndex(SID("svgf_variance_estimated")),
                .width = width,
                .height = height,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("svgf_variance_estimate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 6: Variance-Guided Atrous
    if (params.atrousIterations == 0) {
        auto& pass = graph.AddPass(SID("[SVGF] ATrous Bypass"), VK_PIPELINE_STAGE_2_COPY_BIT, Render::ResourceCategory::Denoising);
        pass.ReadCopyImage(SID("svgf_color_accum"));
        pass.WriteCopyImage(SID("svgf_denoised"));
        pass.Execute([&graph, width, height](VkCommandBuffer cmd) {
            VkImage src = graph.GetImageHandle(SID("svgf_color_accum"));
            VkImage dst = graph.GetImageHandle(SID("svgf_denoised"));
            VkOffset3D extent = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
            VkImageCopy2 region{};
            region.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.srcOffset = {};
            region.dstOffset = {};
            region.extent = {static_cast<uint32_t>(extent.x), static_cast<uint32_t>(extent.y), 1};
            VkCopyImageInfo2 copyInfo{};
            copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
            copyInfo.srcImage = src;
            copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyInfo.dstImage = dst;
            copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            copyInfo.regionCount = 1;
            copyInfo.pRegions = &region;
            vkCmdCopyImage2(cmd, &copyInfo);
        });
    }
    else {
        constexpr int32_t ATROUS_PASS_COUNT = 4;
        const StringID atrousColorIn[ATROUS_PASS_COUNT] = {SID("svgf_color_accum"), SID("svgf_atrous_0"), SID("svgf_atrous_1"), SID("svgf_atrous_2")};
        const StringID atrousColorOut[ATROUS_PASS_COUNT] = {SID("svgf_atrous_0"), SID("svgf_atrous_1"), SID("svgf_atrous_2"), SID("svgf_denoised")};
        const StringID atrousVarIn[ATROUS_PASS_COUNT] = {SID("svgf_variance_estimated"), SID("svgf_atrous_variance_0"), SID("svgf_atrous_variance_1"), SID("svgf_atrous_variance_2")};
        const StringID atrousVarOut[ATROUS_PASS_COUNT] = {SID("svgf_atrous_variance_0"), SID("svgf_atrous_variance_1"), SID("svgf_atrous_variance_2"), SID("svgf_atrous_variance_last")};
        const char* atrousNames[ATROUS_PASS_COUNT] = {"[SVGF] ATrous 0", "[SVGF] ATrous 1", "[SVGF] ATrous 2", "[SVGF] ATrous 3"};

        for (int32_t i = 0; i < params.atrousIterations; i++) {
            const bool isLast = (i == params.atrousIterations - 1);
            const StringID colorIn = atrousColorIn[i];
            const StringID colorOut = isLast ? atrousColorOut[ATROUS_PASS_COUNT - 1] : atrousColorOut[i];
            const StringID varIn = atrousVarIn[i];
            const StringID varOut = isLast ? atrousVarOut[ATROUS_PASS_COUNT - 1] : atrousVarOut[i];
            const uint32_t stepSize = 1u << static_cast<uint32_t>(i);

            auto& pass = graph.AddPass(SID(atrousNames[i]), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Denoising);
            pass.ReadBuffer(SCENE_DATA_BUFFER);
            pass.ReadSampledImage(gbufferOne);
            pass.ReadSampledImage(depth);
            if (colorIn == colorOut) {
                pass.ReadWriteImage(colorIn);
            }
            else {
                pass.ReadSampledImage(colorIn);
                pass.WriteStorageImage(colorOut);
            }
            pass.ReadSampledImage(varIn);
            pass.WriteStorageImage(varOut);
            pass.Execute([&graph, pipelineManager, colorIn, colorOut, varIn, varOut, gbufferOne, depth,
                    width, height, stepSize, params](VkCommandBuffer cmd) {
                    SVGFAtrousWaveletPushConstant pc{
                        .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                        .inputColorIndex = graph.GetSampledImageViewDescriptorIndex(colorIn),
                        .outputColorIndex = graph.GetStorageImageViewDescriptorIndex(colorOut),
                        .inputVarianceIndex = graph.GetSampledImageViewDescriptorIndex(varIn),
                        .outputVarianceIndex = graph.GetStorageImageViewDescriptorIndex(varOut),
                        .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                        .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                        .stepSize = stepSize,
                        .width = width,
                        .height = height,
                        .sigmaLuminance = params.sigmaLuminance,
                        .sigmaNormal = params.sigmaNormal,
                        .sigmaDepth = params.sigmaDepth,
                        };
                    const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("svgf_atrous_wavelet"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
                    vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
                });
        }
    }

    // Remodulate: multiply denoised irradiance by albedo from gbuffer
    {
        auto& pass = graph.AddPass(SID("[SVGF] Remodulate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::ResourceCategory::Denoising);
        pass.ReadSampledImage(SID("svgf_denoised"));
        pass.ReadSampledImage(gbufferTwo);
        pass.ReadSampledImage(depth);
        pass.WriteStorageImage(lightingOutput);
        pass.Execute([&graph, pipelineManager, gbufferTwo, depth, lightingOutput, width, height](VkCommandBuffer cmd) {
            SVGFRemodulatePushConstant pc{
                .irradianceIndex = graph.GetSampledImageViewDescriptorIndex(SID("svgf_denoised")),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetDepthOnlySampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(lightingOutput),
                .width = width,
                .height = height,
            };
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry(SID("svgf_remodulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }
}
} // Render
