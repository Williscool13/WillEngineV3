//
// Created by William on 2026-07-09.
//

#include "render/passes/reflection_denoise_passes.h"

#include "reflection_passes.h"
#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"
#include "render/shaders/relax_interop.h"

namespace Render
{
void SetupReflectionRELAXDenoiser(RenderGraph& graph,
                                  PipelineManager* pipelineManager,
                                  const Core::ViewFamily& viewFamily,
                                  Core::Array<uint32_t, 2> renderExtent,
                                  const RenderTargets& targets,
                                  const Core::RELAXParams& params,
                                  uint64_t frameNumber,
                                  uint32_t activeCheckerboardField,
                                  float checkerboardResolveAccumSpeed,
                                  const Core::RTReflectionConfiguration& reflectionConfig,
                                  float brdfRoughnessMax)
{
    const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig, brdfRoughnessMax);
    if (reflectionRoughnessMax < 0.0f || !reflectionConfig.bDenoiserEnabled || !graph.HasTexture(REFLECTION_SPEC_NOISY_TARGET)) { return; }

    const bool bCheckerboard = activeCheckerboardField != 0u;
    const bool bPrepass = params.enablePrepass || bCheckerboard;
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];
    const uint32_t tilesW = (width + 15) / 16;
    const uint32_t tilesH = (height + 15) / 16;

    const StringID gbufferOne = targets.gbufferOne;
    const StringID depth = targets.depthCopy;
    const StringID specInput = REFLECTION_SPEC_NOISY_TARGET;
    const StringID diffInput = REFLECTION_SPEC_NOISY_TARGET;

    const glm::mat4& view = viewFamily.mainView.currentViewData.view;
    const glm::mat4& proj = viewFamily.mainView.currentViewData.proj;
    const glm::mat4& prevView = viewFamily.mainView.previousViewData.view;
    const glm::mat4& prevProj = viewFamily.mainView.previousViewData.proj;

    const glm::mat4 invView = glm::inverse(view);
    const glm::mat4 invPrevView = glm::inverse(prevView);
    const float tanHalfFovX = 1.0f / glm::abs(proj[0][0]);
    const float tanHalfFovY = 1.0f / glm::abs(proj[1][1]);

    const glm::mat4 rotView = glm::mat4(glm::mat3(view));

    const glm::vec3 right = glm::vec3(invView[0]);
    const glm::vec3 up = glm::vec3(invView[1]);
    const glm::vec3 forward = -glm::vec3(invView[2]);
    const glm::vec3 prevRight = glm::vec3(invPrevView[0]);
    const glm::vec3 prevUp = glm::vec3(invPrevView[1]);
    const glm::vec3 prevForward = -glm::vec3(invPrevView[2]);

    const glm::vec3 camPos = glm::vec3(invView[3]);
    const glm::vec3 prevCamPos = glm::vec3(invPrevView[3]);
    const glm::vec3 translationDelta = prevCamPos - camPos;

    glm::mat4 viewToWorldPrev = glm::mat4(glm::mat3(invPrevView));
    viewToWorldPrev[3] = glm::vec4(translationDelta, 1.0f);
    const glm::mat4 worldToViewPrev = glm::inverse(viewToWorldPrev);

    glm::mat4 viewToWorld = glm::mat4(glm::mat3(invView));
    viewToWorld[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    const bool bFirstFrame = !graph.HasTexture(SID("refl_relax_spec_illum_history"));

    RelaxDiffuseSpecularConstants rc{};

    rc.gWorldToClip = proj * rotView;
    rc.gWorldToClipPrev = prevProj * worldToViewPrev;

    glm::mat4 worldToViewPrevPosZ = worldToViewPrev;
    worldToViewPrevPosZ[0][2] = -worldToViewPrevPosZ[0][2];
    worldToViewPrevPosZ[1][2] = -worldToViewPrevPosZ[1][2];
    worldToViewPrevPosZ[2][2] = -worldToViewPrevPosZ[2][2];
    worldToViewPrevPosZ[3][2] = -worldToViewPrevPosZ[3][2];
    rc.gWorldToViewPrev = worldToViewPrevPosZ;
    rc.gWorldPrevToWorld = glm::mat4(1.0f);
    rc.gViewToWorld = viewToWorld;

    rc.gRotatorPre = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    rc.gFrustumForward = glm::vec4(forward, 0.0f);
    rc.gFrustumRight = glm::vec4(right * tanHalfFovX, 0.0f);
    rc.gFrustumUp = glm::vec4(up * tanHalfFovY, 0.0f);
    rc.gPrevFrustumForward = glm::vec4(prevForward, 0.0f);
    rc.gPrevFrustumRight = glm::vec4(prevRight * tanHalfFovX, 0.0f);
    rc.gPrevFrustumUp = glm::vec4(prevUp * tanHalfFovY, 0.0f);
    rc.gCameraDelta = glm::vec4(prevCamPos - camPos, 0.0f);
    rc.gMvScale = glm::vec4(0.5f, 0.5f, 1.0f, 0.0f);

    rc.gJitter = glm::vec2(0.0f);
    rc.gResolutionScale = glm::vec2(1.0f);
    rc.gRectOffset = glm::vec2(0.0f);
    rc.gRectSizeInv = glm::vec2(1.0f / width, 1.0f / height);
    rc.gRectSizePrev = glm::vec2(width, height);
    rc.gResourceSizeInv = rc.gRectSizeInv;
    rc.gResourceSizeInvPrev = rc.gRectSizeInv;
    rc.gResourceSize = glm::vec2(width, height);

    rc.gRectSize = glm::ivec2(width, height);

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
    rc.gDiffBlurRadius = params.enablePrepass ? params.diffBlurRadius : 0.0f;
    rc.gSpecBlurRadius = params.enablePrepass ? params.specBlurRadius : 0.0f;
    rc.gLobeAngleFraction = params.lobeAngleFraction;
    rc.gSpecLobeAngleSlack = params.specLobeAngleSlack * (3.14159265358979f / 180.0f);
    rc.gHistoryFixEdgeStoppingNormalPower = params.historyFixEdgeStoppingNormalPower;
    rc.gHistoryFixFrameNum = params.historyFixFrameNum;
    rc.gHistoryThreshold = params.spatialVarianceEstimationHistoryThreshold;
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
    rc.gDiffCheckerboard = bCheckerboard ? 0u : 2u;
    rc.gSpecCheckerboard = bCheckerboard ? 0u : 2u;
    rc.gCheckerboardResolveAccumSpeed = bCheckerboard ? checkerboardResolveAccumSpeed : 0.0f;

    graph.CreateBuffer(SID("refl_relax_constants"), sizeof(RelaxDiffuseSpecularConstants));
    UploadAllocation rcAlloc = graph.AllocateTransient(sizeof(RelaxDiffuseSpecularConstants));
    memcpy(rcAlloc.ptr, &rc, sizeof(RelaxDiffuseSpecularConstants)); {
        auto& pass = graph.AddPass(SID("[Reflection ReLAX] Upload Constants"), VK_PIPELINE_STAGE_2_COPY_BIT, ResourceCategory::Untagged);
        pass.WriteTransferBuffer(SID("refl_relax_constants"));
        pass.Execute([offset = rcAlloc.offset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferCopy2 region{.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2, .srcOffset = offset, .dstOffset = 0, .size = sizeof(RelaxDiffuseSpecularConstants)};
            VkCopyBufferInfo2 info{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = graph.GetTransientUploadBuffer(),
                .dstBuffer = graph.GetBufferHandle(SID("refl_relax_constants")),
                .regionCount = 1,
                .pRegions = &region
            };
            vkCmdCopyBuffer2(cmd, &info);
        });
    }

    const TextureInfo colorInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1};
    const TextureInfo histLenInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo hitDistInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo reprConfInfo{VK_FORMAT_R8_UNORM, width, height, 1};
    const TextureInfo tilesInfo{VK_FORMAT_R8_UNORM, tilesW, tilesH, 1};
    const TextureInfo viewZInfo{VK_FORMAT_R32_SFLOAT, width, height, 1};

    // Pass 0: Generate half-res linearized viewZ
    {
        graph.CreateTexture(SID("refl_relax_viewz"), viewZInfo, {std::nullopt}, true);
        graph.CarryTextureToNextFrame(SID("refl_relax_viewz"), SID("refl_relax_viewz_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

        auto& pass = graph.AddPass(SID("[Reflection ReLAX] Generate ViewZ"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("refl_relax_constants"));
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.WriteStorageImage(SID("refl_relax_viewz"));
        pass.Execute([pipelineManager, depth, gbufferOne, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxGenerateViewZPushConstant pc{
                .constants = graph.GetBufferAddress(SID("refl_relax_constants")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outViewZIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_viewz")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_generate_viewz"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 1: Classify Tiles
    {
        graph.CreateTexture(SID("refl_relax_tiles"), tilesInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass(SID("[Reflection ReLAX] Classify Tiles"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("refl_relax_constants"));
        pass.ReadSampledImage(depth);
        pass.WriteStorageImage(SID("refl_relax_tiles"));
        pass.Execute([pipelineManager, depth, tilesW, tilesH](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxClassifyTilesPushConstant pc{
                .constants = graph.GetBufferAddress(SID("refl_relax_constants")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .tilesOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_tiles")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_classify_tiles"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesW, tilesH, 1);
        });
    }

    // Pass 2: Prepass (checkerboard hole resolve + optional spatial prefilter)
    if (bPrepass) {
        graph.CreateTexture(SID("refl_relax_spec_prepass"), colorInfo, {std::nullopt}, true);
        graph.CreateTexture(SID("refl_relax_diff_prepass"), colorInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass(SID("[Reflection ReLAX] Prepass"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("refl_relax_constants"));
        pass.ReadSampledImage(SID("refl_relax_tiles"));
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(specInput);
        pass.WriteStorageImage(SID("refl_relax_spec_prepass"));
        pass.WriteStorageImage(SID("refl_relax_diff_prepass"));
        pass.Execute([pipelineManager, depth, gbufferOne, specInput, diffInput, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxPrepassPushConstant pc{
                .constants = graph.GetBufferAddress(SID("refl_relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_tiles")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_spec_prepass")),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_diff_prepass")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_prepass"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
        });
    }

    graph.CreateTexture(SID("refl_relax_spec_illum"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_diff_illum"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_spec_fast"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_diff_fast"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_spec_fast_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_diff_fast_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_spec_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_diff_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_history_length"), histLenInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_spec_hit_dist"), hitDistInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_spec_reproj_confidence"), reprConfInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_prev_nr"), colorInfo, {std::nullopt}, true);

    graph.CarryTextureToNextFrame(SID("refl_relax_spec_hist"), SID("refl_relax_spec_illum_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("refl_relax_diff_hist"), SID("refl_relax_diff_illum_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("refl_relax_spec_fast_hist"), SID("refl_relax_spec_fast_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("refl_relax_diff_fast_hist"), SID("refl_relax_diff_fast_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("refl_relax_history_length"), SID("refl_relax_history_length_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("refl_relax_spec_hit_dist"), SID("refl_relax_spec_hit_dist_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("refl_relax_prev_nr"), SID("refl_relax_prev_nr_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

    // Pass 3: Temporal Accumulation
    {
        const StringID specIn = bPrepass ? SID("refl_relax_spec_prepass") : specInput;
        const StringID diffIn = bPrepass ? SID("refl_relax_diff_prepass") : diffInput;

        auto& pass = graph.AddPass(SID("[Reflection ReLAX] Temporal Accumulation"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("refl_relax_constants"));
        pass.ReadSampledImage(SID("refl_relax_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(specIn);
        pass.ReadSampledImage(diffIn);
        if (graph.HasTexture(SID("refl_relax_spec_illum_history"))) { pass.ReadSampledImage(SID("refl_relax_spec_illum_history")); }
        if (graph.HasTexture(SID("refl_relax_diff_illum_history"))) { pass.ReadSampledImage(SID("refl_relax_diff_illum_history")); }
        if (graph.HasTexture(SID("refl_relax_spec_fast_history"))) { pass.ReadSampledImage(SID("refl_relax_spec_fast_history")); }
        if (graph.HasTexture(SID("refl_relax_diff_fast_history"))) { pass.ReadSampledImage(SID("refl_relax_diff_fast_history")); }
        if (graph.HasTexture(SID("refl_relax_history_length_history"))) { pass.ReadSampledImage(SID("refl_relax_history_length_history")); }
        if (graph.HasTexture(SID("refl_relax_spec_hit_dist_history"))) { pass.ReadSampledImage(SID("refl_relax_spec_hit_dist_history")); }
        if (graph.HasTexture(SID("refl_relax_prev_nr_history"))) { pass.ReadSampledImage(SID("refl_relax_prev_nr_history")); }
        if (graph.HasTexture(SID("refl_relax_viewz_history"))) {
            pass.ReadSampledImage(SID("refl_relax_viewz_history"));
        }
        else {
            pass.ReadSampledImage(SID("refl_relax_viewz"));
        }
        pass.WriteStorageImage(SID("refl_relax_spec_illum"));
        pass.WriteStorageImage(SID("refl_relax_diff_illum"));
        pass.WriteStorageImage(SID("refl_relax_spec_fast"));
        pass.WriteStorageImage(SID("refl_relax_diff_fast"));
        pass.WriteStorageImage(SID("refl_relax_history_length"));
        pass.WriteStorageImage(SID("refl_relax_spec_hit_dist"));
        pass.WriteStorageImage(SID("refl_relax_spec_reproj_confidence"));
        pass.WriteStorageImage(SID("refl_relax_prev_nr"));

        pass.Execute([pipelineManager, gbufferOne, depth, specIn, diffIn, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const bool hasHistory = graph.HasTexture(SID("refl_relax_spec_illum_history"));
            const StringID fallbackSpec = hasHistory ? SID("refl_relax_spec_illum_history") : specIn;
            const StringID fallbackDiff = hasHistory ? SID("refl_relax_diff_illum_history") : diffIn;
            const StringID fallbackSpecFast = graph.HasTexture(SID("refl_relax_spec_fast_history")) ? SID("refl_relax_spec_fast_history") : specIn;
            const StringID fallbackDiffFast = graph.HasTexture(SID("refl_relax_diff_fast_history")) ? SID("refl_relax_diff_fast_history") : diffIn;
            const StringID fallbackHistLen = graph.HasTexture(SID("refl_relax_history_length_history")) ? SID("refl_relax_history_length_history") : SID("refl_relax_history_length");
            const StringID fallbackSpecHitD = graph.HasTexture(SID("refl_relax_spec_hit_dist_history")) ? SID("refl_relax_spec_hit_dist_history") : SID("refl_relax_spec_hit_dist");
            const StringID fallbackPrevNR = graph.HasTexture(SID("refl_relax_prev_nr_history")) ? SID("refl_relax_prev_nr_history") : SID("refl_relax_prev_nr");
            const StringID fallbackViewZ = graph.HasTexture(SID("refl_relax_viewz_history")) ? SID("refl_relax_viewz_history") : SID("refl_relax_viewz");

            RelaxTemporalAccumulationPushConstant pc{
                .constants = graph.GetBufferAddress(SID("refl_relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .prevNormalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(fallbackPrevNR),
                .prevViewZIndex = graph.GetSampledImageViewDescriptorIndex(fallbackViewZ),
                .prevHistoryLengthIndex = graph.GetSampledImageViewDescriptorIndex(fallbackHistLen),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                .historySpecFastIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecFast),
                .historyDiffFastIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiffFast),
                .historySpecIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpec),
                .historyDiffIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiff),
                .prevSpecHitDistIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecHitD),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_history_length")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_spec_illum")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_diff_illum")),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_spec_fast")),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_diff_fast")),
                .outSpecHitDistIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_spec_hit_dist")),
                .outSpecReprojConfidenceIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_spec_reproj_confidence")),
                .outPrevNRIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_prev_nr")),
                .confidenceIndex = ~0u,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_temporal_accumulation"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 15) / 16, 1);
        });
    }

    graph.CreateTexture(SID("refl_relax_atrous_spec_0"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_atrous_spec_1"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_atrous_diff_0"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("refl_relax_atrous_diff_1"), colorInfo, {std::nullopt}, true);

    // Pass 4: History Fix
    {
        auto& pass = graph.AddPass(SID("[Reflection ReLAX] History Fix"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("refl_relax_constants"));
        pass.ReadSampledImage(SID("refl_relax_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(SID("refl_relax_history_length"));
        pass.ReadSampledImage(SID("refl_relax_spec_illum"));
        pass.ReadSampledImage(SID("refl_relax_diff_illum"));
        pass.ReadWriteImage(SID("refl_relax_spec_fast"));
        pass.ReadWriteImage(SID("refl_relax_diff_fast"));
        pass.Execute([pipelineManager, gbufferOne, depth, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxHistoryFixPushConstant pc{
                .constants = graph.GetBufferAddress(SID("refl_relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_history_length")),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_spec_illum")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_diff_illum")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_spec_fast")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_diff_fast")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_history_fix"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 5: History Clamping
    {
        const StringID specNoisy = bPrepass ? SID("refl_relax_spec_prepass") : specInput;
        const StringID diffNoisy = bPrepass ? SID("refl_relax_diff_prepass") : diffInput;

        const StringID clampSpecOut = params.enableAntiFirefly ? SID("refl_relax_atrous_spec_0") : SID("refl_relax_spec_hist");
        const StringID clampDiffOut = params.enableAntiFirefly ? SID("refl_relax_atrous_diff_0") : SID("refl_relax_diff_hist");

        auto& pass = graph.AddPass(SID("[Reflection ReLAX] History Clamping"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("refl_relax_constants"));
        pass.ReadSampledImage(SID("refl_relax_tiles"));
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadWriteImage(SID("refl_relax_history_length"));
        pass.ReadSampledImage(SID("refl_relax_spec_fast"));
        pass.ReadSampledImage(SID("refl_relax_diff_fast"));
        pass.ReadSampledImage(SID("refl_relax_spec_illum"));
        pass.ReadSampledImage(SID("refl_relax_diff_illum"));
        if (params.enableAntiFirefly) {
            pass.WriteStorageImage(SID("refl_relax_atrous_spec_0"));
            pass.WriteStorageImage(SID("refl_relax_atrous_diff_0"));
        } else {
            pass.WriteStorageImage(SID("refl_relax_spec_hist"));
            pass.WriteStorageImage(SID("refl_relax_diff_hist"));
        }
        pass.ReadSampledImage(specNoisy);
        pass.ReadSampledImage(diffNoisy);
        pass.WriteStorageImage(SID("refl_relax_spec_fast_hist"));
        pass.WriteStorageImage(SID("refl_relax_diff_fast_hist"));
        pass.Execute([pipelineManager, depth, gbufferOne, specNoisy, diffNoisy, clampSpecOut, clampDiffOut, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxHistoryClampingPushConstant pc{
                .constants = graph.GetBufferAddress(SID("refl_relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_tiles")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_history_length")),
                .specFastIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_spec_fast")),
                .diffFastIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_diff_fast")),
                .specNoisyIndex = graph.GetSampledImageViewDescriptorIndex(specNoisy),
                .diffNoisyIndex = graph.GetSampledImageViewDescriptorIndex(diffNoisy),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_spec_illum")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_diff_illum")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(clampSpecOut),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(clampDiffOut),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_spec_fast_hist")),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_diff_fast_hist")),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_history_length")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_history_clamping"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 6: Anti-Firefly
    if (params.enableAntiFirefly) {
        auto& pass = graph.AddPass(SID("[Reflection ReLAX] Anti-Firefly"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
        pass.ReadBuffer(SID("refl_relax_constants"));
        pass.ReadSampledImage(SID("refl_relax_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(SID("refl_relax_atrous_spec_0"));
        pass.ReadSampledImage(SID("refl_relax_atrous_diff_0"));
        pass.WriteStorageImage(SID("refl_relax_spec_hist"));
        pass.WriteStorageImage(SID("refl_relax_diff_hist"));

        pass.Execute([pipelineManager, gbufferOne, depth, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxAntiFireflyPushConstant pc{
                .constants = graph.GetBufferAddress(SID("refl_relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_atrous_spec_0")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_atrous_diff_0")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_spec_hist")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("refl_relax_diff_hist")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_antifirefly"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 7: A-Trous. Last iteration writes REFLECTION_SPEC_DENOISED_TARGET (spec) and a throwaway diff scratch.
    {
        graph.CreateTexture(REFLECTION_SPEC_DENOISED_TARGET, colorInfo, {std::nullopt}, true);
        graph.CreateTexture(SID("refl_relax_diff_out_unused"), colorInfo, {std::nullopt}, true);

        const int32_t iters = glm::max(1, params.atrousIterations);
        const StringID scratchSpec[2] = {SID("refl_relax_atrous_spec_0"), SID("refl_relax_atrous_spec_1")};
        const StringID scratchDiff[2] = {SID("refl_relax_atrous_diff_0"), SID("refl_relax_atrous_diff_1")};

        for (int32_t i = 0; i < iters; i++) {
            const bool isLast = (i == iters - 1);
            const StringID specIn = (i == 0) ? SID("refl_relax_spec_hist") : scratchSpec[(i - 1) & 1];
            const StringID diffIn = (i == 0) ? SID("refl_relax_diff_hist") : scratchDiff[(i - 1) & 1];
            const StringID specOut = isLast ? REFLECTION_SPEC_DENOISED_TARGET : scratchSpec[i & 1];
            const StringID diffOut = isLast ? SID("refl_relax_diff_out_unused") : scratchDiff[i & 1];
            const uint32_t stepSize = 1u << static_cast<uint32_t>(i);

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("[Reflection ReLAX] ATrous %d", i);

            auto& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::ReSTIR);
            pass.ReadBuffer(SID("refl_relax_constants"));
            pass.ReadSampledImage(SID("refl_relax_tiles"));
            pass.ReadSampledImage(gbufferOne);
            pass.ReadSampledImage(depth);
            pass.ReadSampledImage(SID("refl_relax_history_length"));
            pass.ReadSampledImage(SID("refl_relax_spec_reproj_confidence"));
            pass.ReadSampledImage(specIn);
            pass.ReadSampledImage(diffIn);
            pass.WriteStorageImage(specOut);
            pass.WriteStorageImage(diffOut);

            pass.Execute([pipelineManager, gbufferOne, depth,
                    specIn, diffIn, specOut, diffOut, stepSize, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    RelaxAtrousPushConstant pc{
                        .constants = graph.GetBufferAddress(SID("refl_relax_constants")),
                        .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_tiles")),
                        .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                        .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                        .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_history_length")),
                        .specVarIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                        .diffVarIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                        .specReprojConfidenceIndex = graph.GetSampledImageViewDescriptorIndex(SID("refl_relax_spec_reproj_confidence")),
                        .specIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                        .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                        .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specOut),
                        .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffOut),
                        .stepSize = stepSize,
                    };
                    const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_atrous"));
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
                    vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
                });
        }
    }
}
} // Render
