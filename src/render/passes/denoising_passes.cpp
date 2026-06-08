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
#include "render/shaders/relax_interop.h"

namespace Render
{
void SetupATrousWaveletDenoiser(RenderGraph& graph,
                                PipelineManager* pipelineManager,
                                Core::Array<uint32_t, 2> renderExtent,
                                const RenderTargets& targets,
                                const Core::ReSTIRParams::ATrousParams& params)
{
    const StringID gbufferOne = targets.gbufferOne;
    const StringID depthStencil = targets.depthCopy;
    const StringID lightingOutput = targets.colorOutput;

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
                    .depthIndex = graph.GetSampledImageViewDescriptorIndex(depthStencil),
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
                        const RenderTargets& targets,
                        const Core::ReSTIRParams::SVGFParams& params)
{
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];
    constexpr uint32_t GRADIENT_STRIDE = 3u;
    const uint32_t gradW = (width + GRADIENT_STRIDE - 1) / GRADIENT_STRIDE;
    const uint32_t gradH = (height + GRADIENT_STRIDE - 1) / GRADIENT_STRIDE;

    const StringID gbufferOne = targets.gbufferOne;
    const StringID gbufferTwo = targets.gbufferTwo;
    const StringID depth = targets.depthCopy;
    const StringID lightingOutput = targets.colorOutput;

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
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
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
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .depthHistoryIndex = graph.HasTexture(SID("depth_history")) ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : graph.GetSampledImageViewDescriptorIndex(depth),
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
    } {
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
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .depthHistoryIndex = graph.HasTexture(SID("depth_history")) ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : graph.GetSampledImageViewDescriptorIndex(depth),
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
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
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
                        .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
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
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
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

void SetupRELAXDenoiser(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Array<uint32_t, 2> renderExtent,
                        Core::Array<uint32_t, 2> fullRenderExtent,
                        const RenderTargets& targets,
                        const Core::RELAXParams& params,
                        uint64_t frameNumber,
                        uint32_t remodulateOutputMode,
                        uint32_t pixelScale)
{
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];
    const uint32_t tilesW = (width + 15) / 16;
    const uint32_t tilesH = (height + 15) / 16;

    const StringID gbufferOne = targets.gbufferOne;
    const StringID depth = targets.depthCopy;
    const StringID specInput = targets.intermediateTwo;
    const StringID diffInput = targets.intermediateOne;
    const StringID noisyInput = targets.colorOutput;

    // Build RelaxDiffuseSpecularConstants
    const glm::mat4& view = viewFamily.mainView.currentViewData.view;
    const glm::mat4& proj = viewFamily.mainView.currentViewData.proj;
    const glm::mat4& prevView = viewFamily.mainView.previousViewData.view;
    const glm::mat4& prevProj = viewFamily.mainView.previousViewData.proj;

    const glm::mat4 invView = glm::inverse(view);
    const glm::mat4 invPrevView = glm::inverse(prevView);
    const float tanHalfFovX = 1.0f / glm::abs(proj[0][0]);
    const float tanHalfFovY = 1.0f / glm::abs(proj[1][1]);

    // Camera-relative world space (NRD convention): rotation-only current view, prev view carries only the frame-to-frame delta in its translation.
    const glm::mat4 rotView = glm::mat4(glm::mat3(view));

    // World-space frustum vectors for position reconstruction
    const glm::vec3 right = glm::vec3(invView[0]);
    const glm::vec3 up = glm::vec3(invView[1]);
    const glm::vec3 forward = -glm::vec3(invView[2]);
    const glm::vec3 prevRight = glm::vec3(invPrevView[0]);
    const glm::vec3 prevUp = glm::vec3(invPrevView[1]);
    const glm::vec3 prevForward = -glm::vec3(invPrevView[2]);

    const glm::vec3 camPos = glm::vec3(invView[3]);
    const glm::vec3 prevCamPos = glm::vec3(invPrevView[3]);
    const glm::vec3 translationDelta = prevCamPos - camPos;

    // gWorldToViewPrev: camera-relative prev view->world (true view->world rotation + prev camera offset), then inverted.
    glm::mat4 viewToWorldPrev = glm::mat4(glm::mat3(invPrevView));
    viewToWorldPrev[3] = glm::vec4(translationDelta, 1.0f);
    const glm::mat4 worldToViewPrev = glm::inverse(viewToWorldPrev);

    // gViewToWorld: rotation-only view->world (camera at origin), used to rotate view-space gbuffer normals to world.
    glm::mat4 viewToWorld = glm::mat4(glm::mat3(invView));
    viewToWorld[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    const bool bFirstFrame = !graph.HasTexture(SID("relax_spec_illum_history"));

    RelaxDiffuseSpecularConstants rc{};

    rc.gWorldToClip = proj * rotView;
    rc.gWorldToClipPrev = prevProj * worldToViewPrev;

    // gWorldToViewPrev must emit positive viewZ (NRD convention) to match the positive linearized depths it is compared against; negate the z-output row of the camera-relative world->prevView matrix.
    glm::mat4 worldToViewPrevPosZ = worldToViewPrev;
    worldToViewPrevPosZ[0][2] = -worldToViewPrevPosZ[0][2];
    worldToViewPrevPosZ[1][2] = -worldToViewPrevPosZ[1][2];
    worldToViewPrevPosZ[2][2] = -worldToViewPrevPosZ[2][2];
    worldToViewPrevPosZ[3][2] = -worldToViewPrevPosZ[3][2];
    rc.gWorldToViewPrev = worldToViewPrevPosZ;
    rc.gWorldPrevToWorld = glm::mat4(1.0f);
    rc.gViewToWorld = viewToWorld;

    rc.gRotatorPre = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // identity rotator
    rc.gFrustumForward = glm::vec4(forward, 0.0f);
    rc.gFrustumRight = glm::vec4(right * tanHalfFovX, 0.0f);
    rc.gFrustumUp = glm::vec4(up * tanHalfFovY, 0.0f);
    rc.gPrevFrustumForward = glm::vec4(prevForward, 0.0f);
    rc.gPrevFrustumRight = glm::vec4(prevRight * tanHalfFovX, 0.0f);
    rc.gPrevFrustumUp = glm::vec4(prevUp * tanHalfFovY, 0.0f);
    rc.gCameraDelta = glm::vec4(prevCamPos - camPos, 0.0f);
    rc.gMvScale = glm::vec4(0.5f, 0.5f, 1.0f, 0.0f); // xy: NDC->UV scale; z=1 = use gbuffer world-Z delta directly; w=0 = 2D path

    rc.gJitter = glm::vec2(0.0f);
    rc.gResolutionScale = glm::vec2(1.0f);
    rc.gRectOffset = glm::vec2(0.0f);
    rc.gRectSizeInv = glm::vec2(1.0f / width, 1.0f / height);
    rc.gRectSizePrev = glm::vec2(width, height);
    rc.gResourceSizeInv = rc.gRectSizeInv;
    rc.gResourceSizeInvPrev = rc.gRectSizeInv;
    rc.gResourceSize = glm::vec2(width, height);

    rc.gRectSize = glm::ivec2(width, height);

    // Depth linearization from projection matrix (same as GenerateSceneData)
    rc.depthLinearizeMult = -proj[3][2];
    rc.depthLinearizeAdd = proj[2][2];
    if (rc.depthLinearizeMult * rc.depthLinearizeAdd < 0.0f) { rc.depthLinearizeAdd = -rc.depthLinearizeAdd; }

    rc.gSpecMaxAccumulatedFrameNum = params.specMaxAccumFrames;
    rc.gSpecMaxFastAccumulatedFrameNum = params.specMaxFastAccumFrames;
    rc.gDiffMaxAccumulatedFrameNum = params.diffMaxAccumFrames;
    rc.gDiffMaxFastAccumulatedFrameNum = params.diffMaxFastAccumFrames;
    rc.gDisocclusionThreshold = params.disocclusionThreshold;
    rc.gDisocclusionThresholdAlternate = params.disocclusionThreshold * 2.0f;
    rc.gDenoisingRange = params.denoisingRange;
    rc.gDepthThreshold = params.depthThreshold;
    rc.gRoughnessFraction = params.roughnessFraction;
    rc.gSpecVarianceBoost = params.specVarianceBoost;
    rc.gDiffBlurRadius = params.diffBlurRadius;
    rc.gSpecBlurRadius = params.specBlurRadius;
    rc.gLobeAngleFraction = params.lobeAngleFraction;
    rc.gSpecLobeAngleSlack = params.specLobeAngleSlack;
    rc.gHistoryFixEdgeStoppingNormalPower = params.historyFixEdgeStoppingNormalPower;
    rc.gHistoryFixFrameNum = params.historyFixFrameNum;
    rc.gHistoryFixBasePixelStride = params.historyFixBasePixelStride;
    rc.gFastHistoryClampingSigmaScale = params.fastHistoryClampingSigmaScale;
    rc.gHistoryAccelerationAmount = params.historyAccelerationAmount;
    rc.gHistoryResetTemporalSigmaScale = params.historyResetTemporalSigmaScale;
    rc.gHistoryResetSpatialSigmaScale = params.historyResetSpatialSigmaScale;
    rc.gHistoryResetAmount = params.historyResetAmount;
    rc.gSpecPhiLuminance = params.specPhiLuminance;
    rc.gDiffPhiLuminance = params.diffPhiLuminance;
    rc.gDiffMaxLuminanceRelativeDifference = params.diffMaxLuminanceRelativeDifference;
    rc.gSpecMaxLuminanceRelativeDifference = params.specMaxLuminanceRelativeDifference;
    rc.gLuminanceEdgeStoppingRelaxation = params.luminanceEdgeStoppingRelaxation;
    rc.gNormalEdgeStoppingRelaxation = params.normalEdgeStoppingRelaxation;
    rc.gRoughnessEdgeStoppingRelaxation = params.roughnessEdgeStoppingRelaxation;
    rc.gOrthoMode = 0.0f;
    rc.gUnproject = tanHalfFovY * 2.0f / static_cast<float>(height);
    rc.gFramerateScale = params.framerateScale;
    rc.gMinHitDistanceWeight = params.minHitDistanceWeight;
    rc.gRoughnessEdgeStoppingEnabled = params.roughnessEdgeStoppingEnabled ? 1u : 0u;
    rc.gFrameIndex = static_cast<uint32_t>(frameNumber);
    rc.gResetHistory = bFirstFrame ? 1u : 0u;

    // Upload constants buffer
    graph.CreateBuffer(SID("relax_constants"), sizeof(RelaxDiffuseSpecularConstants));
    UploadAllocation rcAlloc = graph.AllocateTransient(sizeof(RelaxDiffuseSpecularConstants));
    memcpy(rcAlloc.ptr, &rc, sizeof(RelaxDiffuseSpecularConstants)); {
        auto& pass = graph.AddPass(SID("[RELAX] Upload Constants"), VK_PIPELINE_STAGE_2_COPY_BIT, ResourceCategory::Untagged);
        pass.WriteTransferBuffer(SID("relax_constants"));
        pass.Execute([&graph, offset = rcAlloc.offset](VkCommandBuffer cmd) {
            VkBufferCopy2 region{.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2, .srcOffset = offset, .dstOffset = 0, .size = sizeof(RelaxDiffuseSpecularConstants)};
            VkCopyBufferInfo2 info{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = graph.GetTransientUploadBuffer(),
                .dstBuffer = graph.GetBufferHandle(SID("relax_constants")),
                .regionCount = 1,
                .pRegions = &region
            };
            vkCmdCopyBuffer2(cmd, &info);
        });
    }

    // Declare transient textures
    const TextureInfo colorInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1};
    const TextureInfo histLenInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo hitDistInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo reprConfInfo{VK_FORMAT_R8_UNORM, width, height, 1};
    const TextureInfo tilesInfo{VK_FORMAT_R8_UNORM, tilesW, tilesH, 1};


    // Pass 1: Classify Tiles
    {
        graph.CreateTexture(SID("relax_tiles"), tilesInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass(SID("[RELAX] Classify Tiles"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(depth);
        pass.WriteStorageImage(SID("relax_tiles"));
        pass.Execute([&graph, pipelineManager, depth, tilesW, tilesH, pixelScale](VkCommandBuffer cmd) {
            RelaxClassifyTilesPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .tilesOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_tiles")),
                .pixelScale = pixelScale,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_classify_tiles"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesW, tilesH, 1);
        });
    }



    // Pass 2: Prepass (optional spatial prefilter)
    if (params.enablePrepass) {
        graph.CreateTexture(SID("relax_spec_prepass"), colorInfo, {std::nullopt}, true);
        graph.CreateTexture(SID("relax_diff_prepass"), colorInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass(SID("[RELAX] Prepass"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(diffInput);
        pass.WriteStorageImage(SID("relax_spec_prepass"));
        pass.WriteStorageImage(SID("relax_diff_prepass"));
        pass.Execute([&graph, pipelineManager, depth, gbufferOne, specInput, diffInput, width, height, pixelScale](VkCommandBuffer cmd) {
            RelaxPrepassPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_prepass")),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_prepass")),
                .pixelScale = pixelScale,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_prepass"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
        });
    }



    graph.CreateTexture(SID("relax_spec_illum"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_diff_illum"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_spec_fast"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_diff_fast"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_spec_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_diff_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_history_length"), histLenInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_spec_hit_dist"), hitDistInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_spec_reproj_confidence"), reprConfInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_prev_nr"), colorInfo, {std::nullopt}, true);

    // Carry ping-pong history textures to next frame.
    graph.CarryTextureToNextFrame(SID("relax_spec_hist"), SID("relax_spec_illum_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_diff_hist"), SID("relax_diff_illum_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_spec_fast"), SID("relax_spec_fast_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_diff_fast"), SID("relax_diff_fast_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_history_length"), SID("relax_history_length_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_spec_hit_dist"), SID("relax_spec_hit_dist_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_prev_nr"), SID("relax_prev_nr_history"), VK_IMAGE_USAGE_SAMPLED_BIT);


    // Pass 3: Temporal Accumulation
    {
        const StringID specIn = params.enablePrepass ? SID("relax_spec_prepass") : specInput;
        const StringID diffIn = params.enablePrepass ? SID("relax_diff_prepass") : diffInput;

        auto& pass = graph.AddPass(SID("[RELAX] Temporal Accumulation"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(specIn);
        pass.ReadSampledImage(diffIn);
        if (graph.HasTexture(SID("relax_spec_illum_history"))) { pass.ReadSampledImage(SID("relax_spec_illum_history")); }
        if (graph.HasTexture(SID("relax_diff_illum_history"))) { pass.ReadSampledImage(SID("relax_diff_illum_history")); }
        if (graph.HasTexture(SID("relax_spec_fast_history"))) { pass.ReadSampledImage(SID("relax_spec_fast_history")); }
        if (graph.HasTexture(SID("relax_diff_fast_history"))) { pass.ReadSampledImage(SID("relax_diff_fast_history")); }
        if (graph.HasTexture(SID("relax_history_length_history"))) { pass.ReadSampledImage(SID("relax_history_length_history")); }
        if (graph.HasTexture(SID("relax_spec_hit_dist_history"))) { pass.ReadSampledImage(SID("relax_spec_hit_dist_history")); }
        if (graph.HasTexture(SID("relax_prev_nr_history"))) { pass.ReadSampledImage(SID("relax_prev_nr_history")); }
        if (graph.HasTexture(SID("depth_history"))) { pass.ReadSampledImage(SID("depth_history")); }
        pass.WriteStorageImage(SID("relax_spec_illum"));
        pass.WriteStorageImage(SID("relax_diff_illum"));
        pass.WriteStorageImage(SID("relax_spec_fast"));
        pass.WriteStorageImage(SID("relax_diff_fast"));
        pass.WriteStorageImage(SID("relax_history_length"));
        pass.WriteStorageImage(SID("relax_spec_hit_dist"));
        pass.WriteStorageImage(SID("relax_spec_reproj_confidence"));
        pass.WriteStorageImage(SID("relax_prev_nr"));

        pass.Execute([&graph, pipelineManager, gbufferOne, depth, specIn, diffIn, width, height, pixelScale](VkCommandBuffer cmd) {
            const bool hasHistory = graph.HasTexture(SID("relax_spec_illum_history"));
            const StringID fallbackSpec = hasHistory ? SID("relax_spec_illum_history") : specIn;
            const StringID fallbackDiff = hasHistory ? SID("relax_diff_illum_history") : diffIn;
            const StringID fallbackSpecFast = graph.HasTexture(SID("relax_spec_fast_history")) ? SID("relax_spec_fast_history") : specIn;
            const StringID fallbackDiffFast = graph.HasTexture(SID("relax_diff_fast_history")) ? SID("relax_diff_fast_history") : diffIn;
            const StringID fallbackHistLen = graph.HasTexture(SID("relax_history_length_history")) ? SID("relax_history_length_history") : SID("relax_history_length");
            const StringID fallbackSpecHitD = graph.HasTexture(SID("relax_spec_hit_dist_history")) ? SID("relax_spec_hit_dist_history") : SID("relax_spec_hit_dist");
            const StringID fallbackPrevNR = graph.HasTexture(SID("relax_prev_nr_history")) ? SID("relax_prev_nr_history") : SID("relax_prev_nr");
            const StringID fallbackDepth = graph.HasTexture(SID("depth_history")) ? SID("depth_history") : depth;

            RelaxTemporalAccumulationPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .prevNormalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(fallbackPrevNR),
                .prevViewZIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDepth),
                .prevHistoryLengthIndex = graph.GetSampledImageViewDescriptorIndex(fallbackHistLen),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                .historySpecFastIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecFast),
                .historyDiffFastIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiffFast),
                .historySpecIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpec),
                .historyDiffIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiff),
                .prevSpecHitDistIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecHitD),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_history_length")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_illum")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_illum")),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_fast")),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_fast")),
                .outSpecHitDistIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_hit_dist")),
                .outSpecReprojConfidenceIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_reproj_confidence")),
                .outPrevNRIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_prev_nr")),
                .pixelScale = pixelScale,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_temporal_accumulation"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 15) / 16, 1);
        });
    }



    graph.CreateTexture(SID("relax_atrous_spec_0"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_atrous_spec_1"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_atrous_diff_0"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_atrous_diff_1"), colorInfo, {std::nullopt}, true);

    // Pass 4: History Fix
    {
        auto& pass = graph.AddPass(SID("[RELAX] History Fix"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(SID("relax_history_length"));
        pass.ReadSampledImage(SID("relax_spec_illum"));
        pass.ReadSampledImage(SID("relax_diff_illum"));
        pass.WriteStorageImage(SID("relax_atrous_spec_0"));
        pass.WriteStorageImage(SID("relax_atrous_diff_0"));
        pass.Execute([&graph, pipelineManager, gbufferOne, depth, width, height, pixelScale](VkCommandBuffer cmd) {
            RelaxHistoryFixPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_history_length")),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_spec_illum")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_diff_illum")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_atrous_spec_0")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_atrous_diff_0")),
                .pixelScale = pixelScale,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_history_fix"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }


    // ----------------------------------------------------------------
    // Pass 5: History Clamping
    // ----------------------------------------------------------------
    {
        const StringID specNoisy = params.enablePrepass ? SID("relax_spec_prepass") : specInput;
        const StringID diffNoisy = params.enablePrepass ? SID("relax_diff_prepass") : diffInput;

        auto& pass = graph.AddPass(SID("[RELAX] History Clamping"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(depth);
        pass.ReadWriteImage(SID("relax_history_length"));
        pass.ReadWriteImage(SID("relax_spec_fast"));
        pass.ReadWriteImage(SID("relax_diff_fast"));
        pass.ReadSampledImage(SID("relax_atrous_spec_0")); // history-fixed normal history
        pass.ReadSampledImage(SID("relax_atrous_diff_0"));
        pass.ReadSampledImage(specNoisy); // noisy preblur reference
        pass.ReadSampledImage(diffNoisy);
        pass.WriteStorageImage(SID("relax_spec_hist"));
        pass.WriteStorageImage(SID("relax_diff_hist"));
        pass.Execute([&graph, pipelineManager, depth, specNoisy, diffNoisy, width, height, pixelScale](VkCommandBuffer cmd) {
            RelaxHistoryClampingPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_history_length")),
                .specFastIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_spec_fast")),
                .diffFastIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_diff_fast")),
                .specNoisyIndex = graph.GetSampledImageViewDescriptorIndex(specNoisy),
                .diffNoisyIndex = graph.GetSampledImageViewDescriptorIndex(diffNoisy),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_atrous_spec_0")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_atrous_diff_0")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_hist")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_hist")),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_fast")),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_fast")),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_history_length")),
                .pixelScale = pixelScale,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_history_clamping"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 6: Anti-Firefly. RCRS filter applied in-place to the clamped slow history, so the firefly-suppressed result is what gets carried as next frame's history (matches NRD's Copy + Anti-Firefly writing back into SPEC/DIFF_ILLUM_PREV).
    if (params.enableAntiFirefly) {
        auto& pass = graph.AddPass(SID("[RELAX] Anti-Firefly"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadWriteImage(SID("relax_spec_hist"));
        pass.ReadWriteImage(SID("relax_diff_hist"));

        pass.Execute([&graph, pipelineManager, gbufferOne, depth, width, height, pixelScale](VkCommandBuffer cmd) {
            RelaxAntiFireflyPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_spec_hist")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_diff_hist")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_hist")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_hist")),
                .pixelScale = pixelScale,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_antifirefly"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }


    // Pass 7: A-Trous (N iterations). Iteration 0 reads the carried slow history (relax_*_hist).
    // Later iterations ping-pong the atrous scratch buffers. The hist textures are never written by the A-Trous chain, so they survive end-of-frame to be carried as next frame's slow history.
    // Last iteration writes into the intermediates so remodulate can composite them.
    {
        const int32_t iters = glm::max(1, params.atrousIterations);
        const StringID scratchSpec[2] = {SID("relax_atrous_spec_0"), SID("relax_atrous_spec_1")};
        const StringID scratchDiff[2] = {SID("relax_atrous_diff_0"), SID("relax_atrous_diff_1")};

        for (int32_t i = 0; i < iters; i++) {
            const bool isLast = (i == iters - 1);
            const StringID specIn = (i == 0) ? SID("relax_spec_hist") : scratchSpec[(i - 1) & 1];
            const StringID diffIn = (i == 0) ? SID("relax_diff_hist") : scratchDiff[(i - 1) & 1];
            const StringID specOut = isLast ? specInput : scratchSpec[i & 1];
            const StringID diffOut = isLast ? diffInput : scratchDiff[i & 1];
            const uint32_t stepSize = 1u << static_cast<uint32_t>(i);

            char passName[32];
            snprintf(passName, sizeof(passName), "[RELAX] ATrous %d", i);

            auto& pass = graph.AddPass(SID(passName), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
            pass.ReadBuffer(SID("relax_constants"));
            pass.ReadSampledImage(SID("relax_tiles"));
            pass.ReadSampledImage(gbufferOne);
            pass.ReadSampledImage(depth);
            pass.ReadSampledImage(SID("relax_history_length"));
            pass.ReadSampledImage(SID("relax_spec_reproj_confidence"));
            pass.ReadSampledImage(specIn);
            pass.ReadSampledImage(diffIn);
            pass.WriteStorageImage(specOut);
            pass.WriteStorageImage(diffOut);

            pass.Execute([&graph, pipelineManager, gbufferOne, depth,
                    specIn, diffIn, specOut, diffOut, stepSize, width, height, pixelScale](VkCommandBuffer cmd) {
                    RelaxAtrousPushConstant pc{
                        .constants = graph.GetBufferAddress(SID("relax_constants")),
                        .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                        .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                        .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                        .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_history_length")),
                        .specVarIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                        .diffVarIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                        .specReprojConfidenceIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_spec_reproj_confidence")),
                        .specIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                        .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                        .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specOut),
                        .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffOut),
                        .stepSize = stepSize,
                        .pixelScale = pixelScale,
                    };
                    const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_atrous"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
                    vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
                });
        }
    }

    // Pass 8: Remodulate denoised diff/spec into final color
    //   final = diffuse * albedo + specular * specReflectance + emissive
    {
        const StringID gbufferTwo = targets.gbufferTwo;

        auto& pass = graph.AddPass(SID("[RELAX] Remodulate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.ReadSampledImage(diffInput);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(gbufferTwo);
        pass.ReadSampledImage(depth);
        pass.WriteStorageImage(noisyInput);

        const uint32_t fullWidth = fullRenderExtent[0];
        const uint32_t fullHeight = fullRenderExtent[1];
        pass.Execute([&graph, pipelineManager, diffInput, specInput, gbufferOne, gbufferTwo, depth, noisyInput, fullWidth, fullHeight, remodulateOutputMode](VkCommandBuffer cmd) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .sceneDataIndex = 0,
                .diffuseIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specularIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(noisyInput),
                .width = fullWidth,
                .height = fullHeight,
                .outputMode = remodulateOutputMode,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("restir_remodulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (fullWidth + 7) / 8, (fullHeight + 7) / 8, 1);
        });
    }
}
} // Render
