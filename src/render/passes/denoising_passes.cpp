//
// Created by William on 2026-06-03.
//

#include "render/passes/denoising_passes.h"

#include "ddgi_passes.h"
#include "final_gather_passes.h"
#include "reflection_passes.h"
#include "shadow_passes.h"
#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"
#include "render/shaders/relax_interop.h"
#include "render/shaders/reblur_interop.h"
#include "render/render-view/render_view_helpers.h"

namespace Render
{

void SetupRELAXDenoiser(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Array<uint32_t, 2> renderExtent,
                        const RenderTargets& targets,
                        const Core::RELAXParams& params,
                        uint64_t frameNumber,
                        uint32_t remodulateOutputMode,
                        float iblIntensity,
                        uint32_t activeCheckerboardField,
                        float checkerboardResolveAccumSpeed,
                        bool bDDGIApply,
                        const Core::ReflectionConfiguration& reflectionConfig,
                        uint32_t giGatherMode)
{
    const bool bCheckerboard = activeCheckerboardField != 0u;
    // NRD RELAX resolves checkerboard inside the prepass
    const bool bPrepass = params.enablePrepass || bCheckerboard;
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
    rc.gViewToWorld = viewToWorld;

    rc.gRotatorPre = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // identity rotator
    rc.gFrustumForward = glm::vec4(forward, 0.0f);
    rc.gFrustumRight = glm::vec4(right * tanHalfFovX, 0.0f);
    rc.gFrustumUp = glm::vec4(up * tanHalfFovY, 0.0f);
    rc.gPrevFrustumForward = glm::vec4(prevForward, 0.0f);
    rc.gPrevFrustumRight = glm::vec4(prevRight * tanHalfFovX, 0.0f);
    rc.gPrevFrustumUp = glm::vec4(prevUp * tanHalfFovY, 0.0f);
    rc.gCameraDelta = glm::vec4(prevCamPos - camPos, 0.0f);
    rc.gMvScale = glm::vec4(0.5f, 0.5f, 1.0f, 0.0f); // xy: NDC->UV scale; z=1 = use gbuffer view-depth delta (viewZprev - viewZ) directly; w=0 = 2D path

    rc.gRectSizeInv = glm::vec2(1.0f / width, 1.0f / height);
    rc.gRectSizePrev = glm::vec2(width, height);
    rc.gResourceSizeInvPrev = rc.gRectSizeInv;

    rc.gRectSize = glm::ivec2(width, height);

    // Depth linearization from projection matrix (same as GenerateSceneData)
    rc.depthLinearizeMult = -proj[3][2];
    rc.depthLinearizeAdd = proj[2][2];
    if (rc.depthLinearizeMult * rc.depthLinearizeAdd < 0.0f) { rc.depthLinearizeAdd = -rc.depthLinearizeAdd; }

    rc.gSpecMaxAccumulatedFrameNum = params.specMaxAccumFrames;
    rc.gSpecMaxFastAccumulatedFrameNum = params.specMaxFastAccumFrames;
    rc.gDiffMaxAccumulatedFrameNum = params.diffMaxAccumFrames;
    rc.gDiffMaxFastAccumulatedFrameNum = params.diffMaxFastAccumFrames;
    const float jitterDelta = ComputeRelaxJitterDelta(viewFamily.aaConfig.mode, frameNumber);
    const float disocclusionThresholdBonus = (1.0f + jitterDelta) / static_cast<float>(height);
    rc.gDisocclusionThreshold = params.disocclusionThreshold + disocclusionThresholdBonus;
    rc.gDenoisingRange = params.denoisingRange;
    rc.gDepthThreshold = params.depthThreshold;
    rc.gRoughnessFraction = params.roughnessFraction;
    rc.gSpecVarianceBoost = params.specVarianceBoost;
    // Checkerboard forces the prepass to run for hole resolve; "prepass disabled" is radius 0.
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
    // Resolve writes diffuse and specular for the same active pixels; both signals live on field 0.
    rc.gDiffCheckerboard = bCheckerboard ? 0u : 2u;
    rc.gSpecCheckerboard = bCheckerboard ? 0u : 2u;
    rc.gCheckerboardResolveAccumSpeed = bCheckerboard ? checkerboardResolveAccumSpeed : 0.0f;

    // Upload constants buffer
    graph.CreateBuffer(SID("relax_constants"), sizeof(RelaxDiffuseSpecularConstants));
    UploadAllocation rcAlloc = graph.AllocateTransient(sizeof(RelaxDiffuseSpecularConstants));
    memcpy(rcAlloc.ptr, &rc, sizeof(RelaxDiffuseSpecularConstants)); {
        auto& pass = graph.AddPass(SID("[ReLAX] Upload Constants"), VK_PIPELINE_STAGE_2_COPY_BIT, RenderCategory::Untagged);
        pass.WriteTransferBuffer(SID("relax_constants"));
        pass.Execute([offset = rcAlloc.offset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
    const TextureInfo viewZInfo{VK_FORMAT_R32_SFLOAT, width, height, 1};

    // Pass 0: Generate half-res linearized viewZ (necessary cause of GatherRed) + the packed guide the filter chain taps
    {
        graph.CreateTexture(SID("relax_viewz"), viewZInfo, {std::nullopt}, true);
        graph.CarryTextureToNextFrame(SID("relax_viewz"), SID("relax_viewz_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
        graph.CreateTexture(SID("relax_guide"), TextureInfo{VK_FORMAT_R32G32_UINT, width, height, 1}, {std::nullopt}, true);

        auto& pass = graph.AddPass(SID("[ReLAX] Generate ViewZ"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.WriteStorageImage(SID("relax_viewz"));
        pass.WriteStorageImage(SID("relax_guide"));
        pass.Execute([pipelineManager, depth, gbufferOne, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxGenerateViewZPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outViewZIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_viewz")),
                .outGuideIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_guide")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_generate_viewz"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 1: Classify Tiles
    {
        graph.CreateTexture(SID("relax_tiles"), tilesInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass(SID("[ReLAX] Classify Tiles"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_viewz"));
        pass.WriteStorageImage(SID("relax_tiles"));
        pass.Execute([pipelineManager, tilesW, tilesH](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxClassifyTilesPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_viewz")),
                .tilesOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_tiles")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_classify_tiles"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesW, tilesH, 1);
        });
    }


    // Pass 2: Prepass (optional spatial prefilter)
    if (bPrepass) {
        graph.CreateTexture(SID("relax_spec_prepass"), colorInfo, {std::nullopt}, true);
        graph.CreateTexture(SID("relax_diff_prepass"), colorInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass(SID("[ReLAX] Prepass"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(SID("relax_guide"));
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(diffInput);
        pass.WriteStorageImage(SID("relax_spec_prepass"));
        pass.WriteStorageImage(SID("relax_diff_prepass"));
        pass.Execute([pipelineManager, specInput, diffInput, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxPrepassPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .guideIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_guide")),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_prepass")),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_prepass")),
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
    graph.CreateTexture(SID("relax_spec_fast_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_diff_fast_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_spec_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_diff_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_history_length"), histLenInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_spec_hit_dist"), hitDistInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_spec_reproj_confidence"), reprConfInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("relax_prev_nr"), colorInfo, {std::nullopt}, true);

    // Carry ping-pong history textures to next frame.
    graph.CarryTextureToNextFrame(SID("relax_spec_hist"), SID("relax_spec_illum_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_diff_hist"), SID("relax_diff_illum_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_spec_fast_hist"), SID("relax_spec_fast_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_diff_fast_hist"), SID("relax_diff_fast_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_history_length"), SID("relax_history_length_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_spec_hit_dist"), SID("relax_spec_hit_dist_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("relax_prev_nr"), SID("relax_prev_nr_history"), VK_IMAGE_USAGE_SAMPLED_BIT);


    // Pass 3: Temporal Accumulation
    {
        const StringID specIn = bPrepass ? SID("relax_spec_prepass") : specInput;
        const StringID diffIn = bPrepass ? SID("relax_diff_prepass") : diffInput;

        auto& pass = graph.AddPass(SID("[ReLAX] Temporal Accumulation"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(SID("relax_guide"));
        pass.ReadSampledImage(specIn);
        pass.ReadSampledImage(diffIn);
        if (graph.HasTexture(SID("relax_spec_illum_history"))) { pass.ReadSampledImage(SID("relax_spec_illum_history")); }
        if (graph.HasTexture(SID("relax_diff_illum_history"))) { pass.ReadSampledImage(SID("relax_diff_illum_history")); }
        if (graph.HasTexture(SID("relax_spec_fast_history"))) { pass.ReadSampledImage(SID("relax_spec_fast_history")); }
        if (graph.HasTexture(SID("relax_diff_fast_history"))) { pass.ReadSampledImage(SID("relax_diff_fast_history")); }
        if (graph.HasTexture(SID("relax_history_length_history"))) { pass.ReadSampledImage(SID("relax_history_length_history")); }
        if (graph.HasTexture(SID("relax_spec_hit_dist_history"))) { pass.ReadSampledImage(SID("relax_spec_hit_dist_history")); }
        if (graph.HasTexture(SID("relax_prev_nr_history"))) { pass.ReadSampledImage(SID("relax_prev_nr_history")); }
        if (graph.HasTexture(SID("relax_viewz_history"))) {
            pass.ReadSampledImage(SID("relax_viewz_history"));
        }
        else {
            pass.ReadSampledImage(SID("relax_viewz"));
        }
        if (graph.HasTexture(SID("restir_confidence"))) { pass.ReadSampledImage(SID("restir_confidence")); }
        pass.WriteStorageImage(SID("relax_spec_illum"));
        pass.WriteStorageImage(SID("relax_diff_illum"));
        pass.WriteStorageImage(SID("relax_spec_fast"));
        pass.WriteStorageImage(SID("relax_diff_fast"));
        pass.WriteStorageImage(SID("relax_history_length"));
        pass.WriteStorageImage(SID("relax_spec_hit_dist"));
        pass.WriteStorageImage(SID("relax_spec_reproj_confidence"));
        pass.WriteStorageImage(SID("relax_prev_nr"));

        pass.Execute([pipelineManager, gbufferOne, specIn, diffIn, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const bool hasHistory = graph.HasTexture(SID("relax_spec_illum_history"));
            const StringID fallbackSpec = hasHistory ? SID("relax_spec_illum_history") : specIn;
            const StringID fallbackDiff = hasHistory ? SID("relax_diff_illum_history") : diffIn;
            const StringID fallbackSpecFast = graph.HasTexture(SID("relax_spec_fast_history")) ? SID("relax_spec_fast_history") : specIn;
            const StringID fallbackDiffFast = graph.HasTexture(SID("relax_diff_fast_history")) ? SID("relax_diff_fast_history") : diffIn;
            const StringID fallbackHistLen = graph.HasTexture(SID("relax_history_length_history")) ? SID("relax_history_length_history") : SID("relax_history_length");
            const StringID fallbackSpecHitD = graph.HasTexture(SID("relax_spec_hit_dist_history")) ? SID("relax_spec_hit_dist_history") : SID("relax_spec_hit_dist");
            const StringID fallbackPrevNR = graph.HasTexture(SID("relax_prev_nr_history")) ? SID("relax_prev_nr_history") : SID("relax_prev_nr");
            const StringID fallbackViewZ = graph.HasTexture(SID("relax_viewz_history")) ? SID("relax_viewz_history") : SID("relax_viewz");

            RelaxTemporalAccumulationPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .guideIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_guide")),
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
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_history_length")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_illum")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_illum")),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_fast")),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_fast")),
                .outSpecHitDistIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_hit_dist")),
                .outSpecReprojConfidenceIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_reproj_confidence")),
                .outPrevNRIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_prev_nr")),
                .confidenceIndex = graph.HasTexture(SID("restir_confidence")) ? graph.GetSampledImageViewDescriptorIndex(SID("restir_confidence")) : ~0u,
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

    // Pass 4: History Fix. Filters the slow history but writes the responsive (fast) textures in place,
    // only at short-history pixels; clamping then promotes the fixed responsive value into the slow output.
    {
        auto& pass = graph.AddPass(SID("[ReLAX] History Fix"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(SID("relax_guide"));
        pass.ReadSampledImage(SID("relax_history_length"));
        pass.ReadSampledImage(SID("relax_spec_illum"));
        pass.ReadSampledImage(SID("relax_diff_illum"));
        pass.ReadWriteImage(SID("relax_spec_fast"));
        pass.ReadWriteImage(SID("relax_diff_fast"));
        pass.Execute([pipelineManager, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxHistoryFixPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .guideIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_guide")),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_history_length")),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_spec_illum")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_diff_illum")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_fast")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_fast")),
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
        const StringID specNoisy = bPrepass ? SID("relax_spec_prepass") : specInput;
        const StringID diffNoisy = bPrepass ? SID("relax_diff_prepass") : diffInput;

        // With anti-firefly enabled the clamped slow output goes into the atrous scratch and anti-firefly produces relax_*_hist.
        // Without it, the clamping output is the carried slow history directly.
        const StringID clampSpecOut = params.enableAntiFirefly ? SID("relax_atrous_spec_0") : SID("relax_spec_hist");
        const StringID clampDiffOut = params.enableAntiFirefly ? SID("relax_atrous_diff_0") : SID("relax_diff_hist");

        auto& pass = graph.AddPass(SID("[ReLAX] History Clamping"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(SID("relax_viewz"));
        pass.ReadWriteImage(SID("relax_history_length"));
        pass.ReadSampledImage(SID("relax_spec_fast"));
        pass.ReadSampledImage(SID("relax_diff_fast"));
        pass.ReadSampledImage(SID("relax_spec_illum")); // raw TA slow history
        pass.ReadSampledImage(SID("relax_diff_illum"));
        if (params.enableAntiFirefly) {
            pass.WriteStorageImage(SID("relax_atrous_spec_0"));
            pass.WriteStorageImage(SID("relax_atrous_diff_0"));
        } else {
            pass.WriteStorageImage(SID("relax_spec_hist"));
            pass.WriteStorageImage(SID("relax_diff_hist"));
        }
        pass.ReadSampledImage(specNoisy); // noisy preblur reference
        pass.ReadSampledImage(diffNoisy);
        pass.WriteStorageImage(SID("relax_spec_fast_hist"));
        pass.WriteStorageImage(SID("relax_diff_fast_hist"));
        pass.Execute([pipelineManager, specNoisy, diffNoisy, clampSpecOut, clampDiffOut, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxHistoryClampingPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_viewz")),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_history_length")),
                .specFastIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_spec_fast")),
                .diffFastIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_diff_fast")),
                .specNoisyIndex = graph.GetSampledImageViewDescriptorIndex(specNoisy),
                .diffNoisyIndex = graph.GetSampledImageViewDescriptorIndex(diffNoisy),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_spec_illum")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_diff_illum")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(clampSpecOut),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(clampDiffOut),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_fast_hist")),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_fast_hist")),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_history_length")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_history_clamping"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 6: Anti-Firefly. RCRS filter from the clamped slow history (in the history-fix scratch) into relax_*_hist, so the firefly-suppressed result is what gets carried as next frame's history (matches NRD's Copy + Anti-Firefly writing into SPEC/DIFF_ILLUM_PREV).
    // Distinct input/output textures: the shared-memory preload reads a border from neighboring workgroups, so in-place filtering would race.
    if (params.enableAntiFirefly) {
        auto& pass = graph.AddPass(SID("[ReLAX] Anti-Firefly"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(SID("relax_constants"));
        pass.ReadSampledImage(SID("relax_tiles"));
        pass.ReadSampledImage(SID("relax_viewz"));
        pass.ReadSampledImage(SID("relax_atrous_spec_0"));
        pass.ReadSampledImage(SID("relax_atrous_diff_0"));
        pass.WriteStorageImage(SID("relax_spec_hist"));
        pass.WriteStorageImage(SID("relax_diff_hist"));

        pass.Execute([pipelineManager, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxAntiFireflyPushConstant pc{
                .constants = graph.GetBufferAddress(SID("relax_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_viewz")),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_atrous_spec_0")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_atrous_diff_0")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_spec_hist")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("relax_diff_hist")),
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
        const int32_t chromaIters = params.bChromaAtrous ? glm::clamp(params.chromaAtrousIterations, 1, 4) : 0;
        const StringID scratchSpec[2] = {SID("relax_atrous_spec_0"), SID("relax_atrous_spec_1")};
        const StringID scratchDiff[2] = {SID("relax_atrous_diff_0"), SID("relax_atrous_diff_1")};

        for (int32_t i = 0; i < iters; i++) {
            const bool isLast = (i == iters - 1);
            const StringID specIn = (i == 0) ? SID("relax_spec_hist") : scratchSpec[(i - 1) & 1];
            const StringID diffIn = (i == 0) ? SID("relax_diff_hist") : scratchDiff[(i - 1) & 1];
            const StringID specOut = isLast ? specInput : scratchSpec[i & 1];

            const StringID diffOut = (isLast && chromaIters == 0) ? diffInput : scratchDiff[i & 1];
            const uint32_t stepSize = 1u << static_cast<uint32_t>(i);

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReLAX] ATrous %d", i);

            auto& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
            pass.ReadBuffer(SID("relax_constants"));
            pass.ReadSampledImage(SID("relax_tiles"));
            pass.ReadSampledImage(SID("relax_guide"));
            pass.ReadSampledImage(SID("relax_history_length"));
            pass.ReadSampledImage(SID("relax_spec_reproj_confidence"));
            pass.ReadSampledImage(specIn);
            pass.ReadSampledImage(diffIn);
            pass.WriteStorageImage(specOut);
            pass.WriteStorageImage(diffOut);

            pass.Execute([pipelineManager,
                    specIn, diffIn, specOut, diffOut, stepSize, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    RelaxAtrousPushConstant pc{
                        .constants = graph.GetBufferAddress(SID("relax_constants")),
                        .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                        .guideIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_guide")),
                        .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_history_length")),
                        .specVarIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                        .diffVarIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                        .specReprojConfidenceIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_spec_reproj_confidence")),
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

        constexpr uint32_t chromaStrides[] = {32u, 64u, 128u, 256u};
        for (int32_t c = 0; c < chromaIters; c++) {
            const bool isLastChroma = (c == chromaIters - 1);
            const StringID diffIn = scratchDiff[(iters - 1 + c) & 1];
            const StringID diffOut = isLastChroma ? diffInput : scratchDiff[(iters + c) & 1];
            const uint32_t stepSize = chromaStrides[c];

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReLAX] ATrous Chroma %d", c);

            auto& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
            pass.ReadBuffer(SID("relax_constants"));
            pass.ReadSampledImage(SID("relax_tiles"));
            pass.ReadSampledImage(SID("relax_guide"));
            pass.ReadSampledImage(SID("relax_history_length"));
            pass.ReadSampledImage(diffIn);
            pass.WriteStorageImage(diffOut);

            pass.Execute([pipelineManager, diffIn, diffOut, stepSize, width, height, chromaLumaPower = params.chromaLumaPower](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                RelaxAtrousPushConstant pc{
                    .constants = graph.GetBufferAddress(SID("relax_constants")),
                    .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_tiles")),
                    .guideIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_guide")),
                    .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(SID("relax_history_length")),
                    .specVarIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                    .diffVarIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                    .specReprojConfidenceIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                    .specIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                    .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                    .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(diffOut),
                    .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffOut),
                    .stepSize = stepSize,
                    .chromaLumaPower = chromaLumaPower,
                };
                const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("relax_atrous_chroma"));
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
        const bool bDDGI = bDDGIApply && graph.HasBuffer(DDGI_CASCADES_BUFFER);
        const bool bGIGather = giGatherMode != 0u && graph.HasTexture(GI_GATHER_RESOLVED);
        const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
        const StringID reflectionTarget = graph.HasTexture(REFLECTION_SPEC_DENOISED_TARGET) ? REFLECTION_SPEC_DENOISED_TARGET : REFLECTION_SPEC_NOISY_TARGET;
        const bool bReflection = reflectionRoughnessMax >= 0.0f && graph.HasTexture(reflectionTarget);

        const StringID shadows = targets.shadows;

        auto& pass = graph.AddPass(SID("[ReLAX] Remodulate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.ReadBuffer(LIGHT_DATA_BUFFER);
        pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
        if (graph.HasBuffer(SID("world_grid_probe_grid"))) { pass.ReadBuffer(SID("world_grid_probe_grid")); }
        pass.ReadSampledImage(diffInput);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(gbufferTwo);
        pass.ReadSampledImage(depth);
        if (shadows != StringID{}) {
            pass.ReadSampledImage(shadows);
        }
        if (bDDGI) {
            AddDDGISampleDependencies(graph, pass);
        }
        if (bReflection) {
            pass.ReadSampledImage(reflectionTarget);
        }
        if (bGIGather) {
            pass.ReadSampledImage(GI_GATHER_RESOLVED);
            pass.ReadSampledImage(GI_GATHER_DATA);
        }
        pass.WriteStorageImage(noisyInput);

        const int32_t skyboxIndex = viewFamily.skyboxIndex;
        const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
        const bool bProbeBrute = viewFamily.bReflectionProbeBruteForce;
        pass.Execute([pipelineManager, diffInput, specInput, gbufferOne, gbufferTwo, depth, noisyInput, width, height, remodulateOutputMode, skyboxIndex, iblIntensity, bDDGI, shadows, bReflection, reflectionRoughnessMax, reflectionTarget, bGIGather, giGatherMode, reflectionProbeCount, bProbeBrute](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
                .sceneDataIndex = 0,
                .diffuseIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specularIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(noisyInput),
                .width = width,
                .height = height,
                .outputMode = remodulateOutputMode,
                .skyboxIndex = skyboxIndex,
                .iblIntensity = iblIntensity,
                .ddgiCascades = bDDGI ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
                .bDDGIApply = bDDGI ? 1u : 0u,
                .shadowsIndex = shadows != StringID{} ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                .reflectionIndex = bReflection ? graph.GetSampledImageViewDescriptorIndex(reflectionTarget) : ~0x0u,
                .reflectionRoughnessMax = reflectionRoughnessMax,
                .giResolvedIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_RESOLVED) : ~0x0u,
                .giDataIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_DATA) : ~0x0u,
                .giGatherMode = bGIGather ? giGatherMode : 0u,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
                .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer(SID("world_grid_probe_grid"))) ? graph.GetBufferAddress(SID("world_grid_probe_grid")) : 0,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("restir_remodulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }
}

void SetupReBLURDenoiser(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Array<uint32_t, 2> renderExtent,
                         const RenderTargets& targets,
                         const Core::ReBLURParams& params,
                         uint64_t frameNumber,
                         uint32_t remodulateOutputMode,
                         float iblIntensity,
                         uint32_t activeCheckerboardField,
                         float checkerboardResolveAccumSpeed,
                         bool bDDGIApply,
                         const Core::ReflectionConfiguration& reflectionConfig,
                          uint32_t giGatherMode)
{
    const bool bCheckerboard = activeCheckerboardField != 0u;
    // NRD resolves checkerboard inside the prepass, so it must run when checkerboard is on
    // (with radius 0 when the user disabled it; the shader gates the Poisson loops on radius).
    const bool bPrepass = params.enablePrepass || bCheckerboard;
    const uint32_t width = renderExtent[0];
    const uint32_t height = renderExtent[1];
    const uint32_t tilesW = (width + 15) / 16;
    const uint32_t tilesH = (height + 15) / 16;

    const StringID gbufferOne = targets.gbufferOne;
    const StringID depth = targets.depthCopy;
    const StringID specInput = targets.intermediateTwo;
    const StringID diffInput = targets.intermediateOne;
    const StringID noisyInput = targets.colorOutput;

    // Build ReblurDiffuseSpecularConstants (geometry block matches RELAX so relax_utils helpers are reused).
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

    const bool bFirstFrame = !graph.HasTexture(SID("reblur_spec_illum_history"));

    ReblurDiffuseSpecularConstants rc{};

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

    rc.gHitDistParams = glm::vec4(params.hitDistA, params.hitDistB, params.hitDistC, params.hitDistD);
    rc.gConvergenceSettings = glm::vec4(params.convergenceS, params.convergenceB, params.convergenceP, 0.0f);
    rc.gAntilagSettings = glm::vec2(params.antilagLuminanceSigmaScale, params.antilagLuminanceSensitivity);
    rc.gSpecProbabilityThresholdsForMvModification = glm::vec2(params.specProbThresholdMvLow, params.specProbThresholdMvHigh);

    rc.gMaxAccumulatedFrameNum = params.maxAccumulatedFrameNum;
    rc.gMaxFastAccumulatedFrameNum = params.maxFastAccumulatedFrameNum;
    // Zero-init would give responsiveFactor ~1 (no-op); these defaults invert that into max responsiveness.
    rc.gResponsiveAccumulationInvRoughnessThreshold = 1000.0f;
    rc.gResponsiveAccumulationMinAccumulatedFrameNum = 3u;
    rc.gMaxStabilizedFrameNum = params.enableTemporalStabilization ? params.maxStabilizedFrameNum : 0.0f;
    rc.gDisocclusionThreshold = params.disocclusionThreshold;
    rc.gDisocclusionThresholdAlternate = params.disocclusionThresholdAlternate;
    rc.gDenoisingRange = params.denoisingRange;
    rc.gPlaneDistSensitivity = params.planeDistanceSensitivity;
    rc.gFramerateScale = params.framerateScale;
    rc.gMinBlurRadius = params.minBlurRadius;
    rc.gMaxBlurRadius = params.maxBlurRadius;
    // Checkerboard forces the prepass to run for hole resolve; "prepass disabled" is radius 0.
    rc.gDiffPrepassBlurRadius = params.enablePrepass ? params.diffusePrepassBlurRadius : 0.0f;
    rc.gSpecPrepassBlurRadius = params.enablePrepass ? params.specularPrepassBlurRadius : 0.0f;
    rc.gLobeAngleFraction = params.lobeAngleFraction;
    rc.gRoughnessFraction = params.roughnessFraction;
    rc.gHistoryFixFrameNum = params.historyFixFrameNum;
    rc.gHistoryFixBasePixelStride = params.historyFixBasePixelStride;
    rc.gFastHistoryClampingSigmaScale = params.fastHistoryClampingSigmaScale;
    rc.gStabilizationStrength = params.stabilizationStrength;
    rc.gFireflySuppressorMinRelativeScale = params.fireflySuppressorMinRelativeScale;
    rc.gMinHitDistanceWeight = params.minHitDistanceWeight;
    rc.gOrthoMode = 0.0f;
    rc.gUnproject = tanHalfFovY * 2.0f / static_cast<float>(height);
    rc.gFrameIndex = static_cast<uint32_t>(frameNumber);
    rc.gResetHistory = bFirstFrame ? 1u : 0u;
    rc.gAntiFirefly = params.enableAntiFirefly ? 1u : 0u;
    rc.gStabilizationFireflyCleanup = params.enableStabilizationFireflyCleanup ? 1u : 0u;
    rc.gHitDistanceReconstructionMode = static_cast<uint32_t>(params.hitDistanceReconstructionMode);
    // Resolve writes diffuse and specular for the same active pixels; both signals live on field 0.
    rc.gCheckerboard = bCheckerboard ? 0u : 2u;
    rc.gCheckerboardResolveAccumSpeed = bCheckerboard ? checkerboardResolveAccumSpeed : 0.0f;

    // Upload constants buffer
    graph.CreateBuffer(SID("reblur_constants"), sizeof(ReblurDiffuseSpecularConstants));
    UploadAllocation rcAlloc = graph.AllocateTransient(sizeof(ReblurDiffuseSpecularConstants));
    memcpy(rcAlloc.ptr, &rc, sizeof(ReblurDiffuseSpecularConstants)); {
        auto& pass = graph.AddPass(SID("[ReBLUR] Upload Constants"), VK_PIPELINE_STAGE_2_COPY_BIT, RenderCategory::Untagged);
        pass.WriteTransferBuffer(SID("reblur_constants"));
        pass.Execute([offset = rcAlloc.offset](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            VkBufferCopy2 region{.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2, .srcOffset = offset, .dstOffset = 0, .size = sizeof(ReblurDiffuseSpecularConstants)};
            VkCopyBufferInfo2 info{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
                .srcBuffer = graph.GetTransientUploadBuffer(),
                .dstBuffer = graph.GetBufferHandle(SID("reblur_constants")),
                .regionCount = 1,
                .pRegions = &region
            };
            vkCmdCopyBuffer2(cmd, &info);
        });
    }

    const TextureInfo colorInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1};
    const TextureInfo histLenInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo hitDistInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo tilesInfo{VK_FORMAT_R8_UNORM, tilesW, tilesH, 1};
    const TextureInfo viewZInfo{VK_FORMAT_R32_SFLOAT, width, height, 1};

    // Pass 0: Generate viewZ
    {
        graph.CreateTexture(SID("reblur_viewz"), viewZInfo, {std::nullopt}, true);
        graph.CarryTextureToNextFrame(SID("reblur_viewz"), SID("reblur_viewz_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

        auto& pass = graph.AddPass(SID("[ReBLUR] Generate ViewZ"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SID("reblur_constants"));
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.WriteStorageImage(SID("reblur_viewz"));
        pass.Execute([pipelineManager, depth, gbufferOne, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurGenerateViewZPushConstant pc{
                .constants = graph.GetBufferAddress(SID("reblur_constants")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outViewZIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_viewz")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("reblur_generate_viewz"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 1: Classify tiles
    {
        graph.CreateTexture(SID("reblur_tiles"), tilesInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass(SID("[ReBLUR] Classify Tiles"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SID("reblur_constants"));
        pass.ReadSampledImage(depth);
        pass.WriteStorageImage(SID("reblur_tiles"));
        pass.Execute([pipelineManager, depth, tilesW, tilesH](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurClassifyTilesPushConstant pc{
                .constants = graph.GetBufferAddress(SID("reblur_constants")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .tilesOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_tiles")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("reblur_classify_tiles"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesW, tilesH, 1);
        });
    }

    // Pass 2: Front-end pack (raw RGB + hitDist -> YCoCg + hitDist)
    graph.CreateTexture(SID("reblur_spec_packed"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_diff_packed"), colorInfo, {std::nullopt}, true);
    {
        auto& pass = graph.AddPass(SID("[ReBLUR] Pack"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SID("reblur_constants"));
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(diffInput);
        pass.WriteStorageImage(SID("reblur_spec_packed"));
        pass.WriteStorageImage(SID("reblur_diff_packed"));
        pass.Execute([pipelineManager, depth, gbufferOne, specInput, diffInput, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurPackPushConstant pc{
                .constants = graph.GetBufferAddress(SID("reblur_constants")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_spec_packed")),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_diff_packed")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("reblur_pack"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 3: Prepass (optional; forced on for checkerboard hole resolve)
    if (bPrepass) {
        graph.CreateTexture(SID("reblur_spec_prepass"), colorInfo, {std::nullopt}, true);
        graph.CreateTexture(SID("reblur_diff_prepass"), colorInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass(SID("[ReBLUR] Prepass"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SID("reblur_constants"));
        pass.ReadSampledImage(SID("reblur_tiles"));
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(SID("reblur_spec_packed"));
        pass.ReadSampledImage(SID("reblur_diff_packed"));
        pass.WriteStorageImage(SID("reblur_spec_prepass"));
        pass.WriteStorageImage(SID("reblur_diff_prepass"));
        pass.Execute([pipelineManager, depth, gbufferOne, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurPrepassPushConstant pc{
                .constants = graph.GetBufferAddress(SID("reblur_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_tiles")),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_spec_packed")),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_diff_packed")),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_spec_prepass")),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_diff_prepass")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("reblur_prepass"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
        });
    }

    const StringID specIn = bPrepass ? SID("reblur_spec_prepass") : SID("reblur_spec_packed");
    const StringID diffIn = bPrepass ? SID("reblur_diff_prepass") : SID("reblur_diff_packed");

    const TextureInfo data1Info{VK_FORMAT_R8G8_UNORM, width, height, 1};
    const TextureInfo data2Info{VK_FORMAT_R32_UINT, width, height, 1};

    graph.CreateTexture(SID("reblur_spec_accum"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_diff_accum"), colorInfo, {std::nullopt}, true);
    // Fast (responsive) history is NRD-faithful single-channel luma (R16F), not RGBA.
    graph.CreateTexture(SID("reblur_spec_fast"), histLenInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_diff_fast"), histLenInfo, {std::nullopt}, true);
    // History fix writes the fast history reconciled with the fixed slow history; that is what gets carried.
    graph.CreateTexture(SID("reblur_spec_fast_fixed"), histLenInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_diff_fast_fixed"), histLenInfo, {std::nullopt}, true);
    // DATA1 = per-lobe accum frames (RG8), DATA2 = occlusion bits + curvature + vha (R32U);
    // internal data = carried per-lobe accum speeds written by stabilization with antilag feedback.
    graph.CreateTexture(SID("reblur_data1"), data1Info, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_data2"), data2Info, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_internal_data"), data2Info, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_spec_hit_dist"), hitDistInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_prev_nr"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_spec_hfix"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_diff_hfix"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_spec_blur"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_diff_blur"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_spec_hist"), colorInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_diff_hist"), colorInfo, {std::nullopt}, true);
    // Stabilized history is single-channel luma.
    graph.CreateTexture(SID("reblur_spec_luma_stab"), histLenInfo, {std::nullopt}, true);
    graph.CreateTexture(SID("reblur_diff_luma_stab"), histLenInfo, {std::nullopt}, true);

    graph.CarryTextureToNextFrame(SID("reblur_spec_hist"), SID("reblur_spec_illum_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("reblur_diff_hist"), SID("reblur_diff_illum_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("reblur_spec_fast_fixed"), SID("reblur_spec_fast_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("reblur_diff_fast_fixed"), SID("reblur_diff_fast_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("reblur_internal_data"), SID("reblur_internal_data_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("reblur_spec_hit_dist"), SID("reblur_spec_hit_dist_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("reblur_prev_nr"), SID("reblur_prev_nr_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("reblur_spec_luma_stab"), SID("reblur_spec_luma_stab_history"), VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(SID("reblur_diff_luma_stab"), SID("reblur_diff_luma_stab_history"), VK_IMAGE_USAGE_SAMPLED_BIT);

    // Pass 4: Temporal accumulation
    {
        auto& pass = graph.AddPass(SID("[ReBLUR] Temporal Accumulation"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SID("reblur_constants"));
        pass.ReadSampledImage(SID("reblur_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(specIn);
        pass.ReadSampledImage(diffIn);
        if (graph.HasTexture(SID("reblur_spec_illum_history"))) { pass.ReadSampledImage(SID("reblur_spec_illum_history")); }
        if (graph.HasTexture(SID("reblur_diff_illum_history"))) { pass.ReadSampledImage(SID("reblur_diff_illum_history")); }
        if (graph.HasTexture(SID("reblur_spec_fast_history"))) { pass.ReadSampledImage(SID("reblur_spec_fast_history")); }
        if (graph.HasTexture(SID("reblur_diff_fast_history"))) { pass.ReadSampledImage(SID("reblur_diff_fast_history")); }
        if (graph.HasTexture(SID("reblur_internal_data_history"))) { pass.ReadSampledImage(SID("reblur_internal_data_history")); }
        if (graph.HasTexture(SID("reblur_spec_hit_dist_history"))) { pass.ReadSampledImage(SID("reblur_spec_hit_dist_history")); }
        if (graph.HasTexture(SID("reblur_prev_nr_history"))) { pass.ReadSampledImage(SID("reblur_prev_nr_history")); }
        if (graph.HasTexture(SID("reblur_viewz_history"))) { pass.ReadSampledImage(SID("reblur_viewz_history")); }
        else { pass.ReadSampledImage(SID("reblur_viewz")); }
        if (graph.HasTexture(SID("restir_confidence"))) { pass.ReadSampledImage(SID("restir_confidence")); }
        pass.WriteStorageImage(SID("reblur_spec_accum"));
        pass.WriteStorageImage(SID("reblur_diff_accum"));
        pass.WriteStorageImage(SID("reblur_spec_fast"));
        pass.WriteStorageImage(SID("reblur_diff_fast"));
        pass.WriteStorageImage(SID("reblur_data1"));
        pass.WriteStorageImage(SID("reblur_data2"));
        pass.WriteStorageImage(SID("reblur_spec_hit_dist"));
        pass.WriteStorageImage(SID("reblur_prev_nr"));

        pass.Execute([pipelineManager, gbufferOne, depth, specIn, diffIn, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const bool hasHistory = graph.HasTexture(SID("reblur_spec_illum_history"));
            const StringID fallbackSpec = hasHistory ? SID("reblur_spec_illum_history") : specIn;
            const StringID fallbackDiff = hasHistory ? SID("reblur_diff_illum_history") : diffIn;
            // First-frame fallbacks must be format-compatible sources (values unused under gResetHistory).
            const StringID fallbackSpecFast = graph.HasTexture(SID("reblur_spec_fast_history")) ? SID("reblur_spec_fast_history") : SID("reblur_spec_hit_dist");
            const StringID fallbackDiffFast = graph.HasTexture(SID("reblur_diff_fast_history")) ? SID("reblur_diff_fast_history") : SID("reblur_spec_hit_dist");
            const StringID fallbackInternalData = graph.HasTexture(SID("reblur_internal_data_history")) ? SID("reblur_internal_data_history") : SID("reblur_data2");
            const StringID fallbackSpecHitD = graph.HasTexture(SID("reblur_spec_hit_dist_history")) ? SID("reblur_spec_hit_dist_history") : SID("reblur_spec_hit_dist");
            const StringID fallbackPrevNR = graph.HasTexture(SID("reblur_prev_nr_history")) ? SID("reblur_prev_nr_history") : SID("reblur_prev_nr");
            const StringID fallbackViewZ = graph.HasTexture(SID("reblur_viewz_history")) ? SID("reblur_viewz_history") : SID("reblur_viewz");

            ReblurTemporalAccumulationPushConstant pc{
                .constants = graph.GetBufferAddress(SID("reblur_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .prevNormalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(fallbackPrevNR),
                .prevViewZIndex = graph.GetSampledImageViewDescriptorIndex(fallbackViewZ),
                .prevInternalDataIndex = graph.GetSampledImageViewDescriptorIndex(fallbackInternalData),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                .historySpecFastIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecFast),
                .historyDiffFastIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiffFast),
                .historySpecIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpec),
                .historyDiffIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiff),
                .prevSpecHitDistIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecHitD),
                .outData1Index = graph.GetStorageImageViewDescriptorIndex(SID("reblur_data1")),
                .outData2Index = graph.GetStorageImageViewDescriptorIndex(SID("reblur_data2")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_spec_accum")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_diff_accum")),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_spec_fast")),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_diff_fast")),
                .outSpecHitDistIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_spec_hit_dist")),
                .outPrevNRIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_prev_nr")),
                .confidenceIndex = graph.HasTexture(SID("restir_confidence")) ? graph.GetSampledImageViewDescriptorIndex(SID("restir_confidence")) : ~0u,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("reblur_temporal_accumulation"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 15) / 16, 1);
        });
    }

    // Pass 5: History fix
    {
        auto& pass = graph.AddPass(SID("[ReBLUR] History Fix"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SID("reblur_constants"));
        pass.ReadSampledImage(SID("reblur_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(SID("reblur_data1"));
        pass.ReadSampledImage(SID("reblur_spec_accum"));
        pass.ReadSampledImage(SID("reblur_diff_accum"));
        pass.ReadSampledImage(SID("reblur_spec_fast"));
        pass.ReadSampledImage(SID("reblur_diff_fast"));
        pass.WriteStorageImage(SID("reblur_spec_hfix"));
        pass.WriteStorageImage(SID("reblur_diff_hfix"));
        pass.WriteStorageImage(SID("reblur_spec_fast_fixed"));
        pass.WriteStorageImage(SID("reblur_diff_fast_fixed"));
        pass.Execute([pipelineManager, gbufferOne, depth, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurHistoryFixPushConstant pc{
                .constants = graph.GetBufferAddress(SID("reblur_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .data1Index = graph.GetSampledImageViewDescriptorIndex(SID("reblur_data1")),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_spec_accum")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_diff_accum")),
                .specFastIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_spec_fast")),
                .diffFastIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_diff_fast")),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_spec_hfix")),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_diff_hfix")),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_spec_fast_fixed")),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_diff_fast_fixed")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("reblur_history_fix"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Passes 6 & 7: Blur and Post-Blur (one pipeline, isPostBlur toggles radius). Post-blur output is the carried slow history.
    auto addBlur = [&](StringID passName, StringID specSrc, StringID diffSrc, StringID specDst, StringID diffDst, uint32_t isPostBlur) {
        auto& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SID("reblur_constants"));
        pass.ReadSampledImage(SID("reblur_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(SID("reblur_data1"));
        pass.ReadSampledImage(specSrc);
        pass.ReadSampledImage(diffSrc);
        pass.WriteStorageImage(specDst);
        pass.WriteStorageImage(diffDst);
        pass.Execute([pipelineManager, gbufferOne, depth, specSrc, diffSrc, specDst, diffDst, isPostBlur, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurBlurPushConstant pc{
                .constants = graph.GetBufferAddress(SID("reblur_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .data1Index = graph.GetSampledImageViewDescriptorIndex(SID("reblur_data1")),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(specSrc),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffSrc),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specDst),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffDst),
                .isPostBlur = isPostBlur,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("reblur_blur"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    };
    addBlur(SID("[ReBLUR] Blur"), SID("reblur_spec_hfix"), SID("reblur_diff_hfix"), SID("reblur_spec_blur"), SID("reblur_diff_blur"), 0u);
    addBlur(SID("[ReBLUR] Post-Blur"), SID("reblur_spec_blur"), SID("reblur_diff_blur"), SID("reblur_spec_hist"), SID("reblur_diff_hist"), 1u);

    const int32_t chromaIters = params.bChromaAtrous ? glm::clamp(params.chromaAtrousIterations, 1, 4) : 0;
    const StringID stabilizationDiffOut = chromaIters > 0 ? SID("reblur_diff_blur") : diffInput;

    // Pass 8: Temporal stabilization (luma-only stabilized ping-pong, surface + virtual motion via the DATA2
    // occlusion bits, antilag feedback written into the carried internal data, final RGB into intermediates)
    {
        auto& pass = graph.AddPass(SID("[ReBLUR] Temporal Stabilization"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SID("reblur_constants"));
        pass.ReadSampledImage(SID("reblur_tiles"));
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(SID("reblur_data1"));
        pass.ReadSampledImage(SID("reblur_data2"));
        pass.ReadSampledImage(SID("reblur_spec_hit_dist"));
        pass.ReadSampledImage(SID("reblur_spec_hist"));
        pass.ReadSampledImage(SID("reblur_diff_hist"));
        if (graph.HasTexture(SID("reblur_spec_luma_stab_history"))) { pass.ReadSampledImage(SID("reblur_spec_luma_stab_history")); }
        if (graph.HasTexture(SID("reblur_diff_luma_stab_history"))) { pass.ReadSampledImage(SID("reblur_diff_luma_stab_history")); }
        pass.WriteStorageImage(SID("reblur_spec_luma_stab"));
        pass.WriteStorageImage(SID("reblur_diff_luma_stab"));
        pass.WriteStorageImage(SID("reblur_internal_data"));
        pass.WriteStorageImage(specInput);
        pass.WriteStorageImage(stabilizationDiffOut);
        pass.Execute([pipelineManager, gbufferOne, depth, specInput, stabilizationDiffOut, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const StringID fallbackSpecStab = graph.HasTexture(SID("reblur_spec_luma_stab_history")) ? SID("reblur_spec_luma_stab_history") : SID("reblur_spec_hit_dist");
            const StringID fallbackDiffStab = graph.HasTexture(SID("reblur_diff_luma_stab_history")) ? SID("reblur_diff_luma_stab_history") : SID("reblur_spec_hit_dist");
            ReblurStabilizationPushConstant pc{
                .constants = graph.GetBufferAddress(SID("reblur_constants")),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_tiles")),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_spec_hist")),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_diff_hist")),
                .data1Index = graph.GetSampledImageViewDescriptorIndex(SID("reblur_data1")),
                .data2Index = graph.GetSampledImageViewDescriptorIndex(SID("reblur_data2")),
                .specHitDistIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_spec_hit_dist")),
                .prevSpecLumaStabIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecStab),
                .prevDiffLumaStabIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiffStab),
                .outSpecLumaStabIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_spec_luma_stab")),
                .outDiffLumaStabIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_diff_luma_stab")),
                .outSpecFinalIndex = graph.GetStorageImageViewDescriptorIndex(specInput),
                .outDiffFinalIndex = graph.GetStorageImageViewDescriptorIndex(stabilizationDiffOut),
                .outInternalDataIndex = graph.GetStorageImageViewDescriptorIndex(SID("reblur_internal_data")),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("reblur_stabilization"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 8b: Diffuse-only chroma widening, ping-ponging the two spatial scratch buffers.
    {
        constexpr uint32_t chromaStrides[] = {32u, 64u, 128u, 256u};
        for (int32_t c = 0; c < chromaIters; c++) {
            const bool isLastChroma = (c == chromaIters - 1);
            const StringID inTex = (c & 1) ? SID("reblur_diff_hfix") : SID("reblur_diff_blur");
            const StringID outTex = isLastChroma ? diffInput : ((c & 1) ? SID("reblur_diff_blur") : SID("reblur_diff_hfix"));
            const uint32_t stepSize = chromaStrides[c];

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReBLUR] Chroma %d", c);

            auto& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
            pass.ReadBuffer(SID("reblur_constants"));
            pass.ReadSampledImage(SID("reblur_tiles"));
            pass.ReadSampledImage(gbufferOne);
            pass.ReadSampledImage(depth);
            pass.ReadSampledImage(SID("reblur_data1"));
            pass.ReadSampledImage(inTex);
            pass.WriteStorageImage(outTex);

            pass.Execute([pipelineManager, gbufferOne, depth, inTex, outTex, stepSize, width, height, chromaLumaPower = params.chromaLumaPower](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                ReblurChromaPushConstant pc{
                    .constants = graph.GetBufferAddress(SID("reblur_constants")),
                    .tilesIndex = graph.GetSampledImageViewDescriptorIndex(SID("reblur_tiles")),
                    .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .data1Index = graph.GetSampledImageViewDescriptorIndex(SID("reblur_data1")),
                    .diffIndex = graph.GetSampledImageViewDescriptorIndex(inTex),
                    .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(outTex),
                    .stepSize = stepSize,
                    .chromaLumaPower = chromaLumaPower,
                };
                const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("reblur_chroma"));
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
                vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
            });
        }
    }

    // Pass 9: Remodulate denoised diff/spec into final color (reuses the ReSTIR remodulate shader).
    {
        const StringID gbufferTwo = targets.gbufferTwo;
        const bool bDDGI = bDDGIApply && graph.HasBuffer(DDGI_CASCADES_BUFFER);
        const bool bGIGather = giGatherMode != 0u && graph.HasTexture(GI_GATHER_RESOLVED);
        const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
        const StringID reflectionTarget = graph.HasTexture(REFLECTION_SPEC_DENOISED_TARGET) ? REFLECTION_SPEC_DENOISED_TARGET : REFLECTION_SPEC_NOISY_TARGET;
        const bool bReflection = reflectionRoughnessMax >= 0.0f && graph.HasTexture(reflectionTarget);

        const StringID shadows = targets.shadows;

        auto& pass = graph.AddPass(SID("[ReBLUR] Remodulate"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.ReadBuffer(LIGHT_DATA_BUFFER);
        pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
        if (graph.HasBuffer(SID("world_grid_probe_grid"))) { pass.ReadBuffer(SID("world_grid_probe_grid")); }
        pass.ReadSampledImage(diffInput);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(gbufferTwo);
        pass.ReadSampledImage(depth);
        if (shadows != StringID{}) {
            pass.ReadSampledImage(shadows);
        }
        if (bDDGI) {
            AddDDGISampleDependencies(graph, pass);
        }
        if (bReflection) {
            pass.ReadSampledImage(reflectionTarget);
        }
        if (bGIGather) {
            pass.ReadSampledImage(GI_GATHER_RESOLVED);
            pass.ReadSampledImage(GI_GATHER_DATA);
        }
        pass.WriteStorageImage(noisyInput);

        const int32_t skyboxIndex = viewFamily.skyboxIndex;
        const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
        const bool bProbeBrute = viewFamily.bReflectionProbeBruteForce;
        pass.Execute([pipelineManager, diffInput, specInput, gbufferOne, gbufferTwo, depth, noisyInput, width, height, remodulateOutputMode, skyboxIndex, iblIntensity, bDDGI, shadows, bReflection, reflectionRoughnessMax, reflectionTarget, bGIGather, giGatherMode, reflectionProbeCount, bProbeBrute](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .lightData = graph.GetBufferAddress(LIGHT_DATA_BUFFER),
                .sceneDataIndex = 0,
                .diffuseIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specularIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .gbufferTwoIndex = graph.GetSampledImageViewDescriptorIndex(gbufferTwo),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(noisyInput),
                .width = width,
                .height = height,
                .outputMode = remodulateOutputMode,
                .skyboxIndex = skyboxIndex,
                .iblIntensity = iblIntensity,
                .ddgiCascades = bDDGI ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
                .bDDGIApply = bDDGI ? 1u : 0u,
                .shadowsIndex = shadows != StringID{} ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                .reflectionIndex = bReflection ? graph.GetSampledImageViewDescriptorIndex(reflectionTarget) : ~0x0u,
                .reflectionRoughnessMax = reflectionRoughnessMax,
                .giResolvedIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_RESOLVED) : ~0x0u,
                .giDataIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_DATA) : ~0x0u,
                .giGatherMode = bGIGather ? giGatherMode : 0u,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
                .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer(SID("world_grid_probe_grid"))) ? graph.GetBufferAddress(SID("world_grid_probe_grid")) : 0,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry(SID("restir_remodulate"));
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }
}
} // Render