//
// Created by William on 2026-06-03.
//

#include "render/passes/denoising_passes.h"

#include <tracy/Tracy.hpp>

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
    ZoneScoped;
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

    // Declare transient textures
    const TextureInfo colorInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1};
    const TextureInfo histLenInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo hitDistInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo reprConfInfo{VK_FORMAT_R8_UNORM, width, height, 1};
    const TextureInfo tilesInfo{VK_FORMAT_R8_UNORM, tilesW, tilesH, 1};
    const TextureInfo viewZInfo{VK_FORMAT_R32_SFLOAT, width, height, 1};

    // History rings must be declared before anything queries them below.
    graph.CreateVersionedTexture("relax_viewz"_sid, viewZInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("relax_spec_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("relax_diff_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("relax_spec_fast_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("relax_diff_fast_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("relax_history_length"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("relax_spec_hit_dist"_sid, hitDistInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("relax_prev_nr"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);

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

    const bool bFirstFrame = !graph.ResourceHasVersion("relax_spec_hist"_sid, 1);

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
    memcpy(graph.OpenHostBuffer("relax_constants"_sid, sizeof(RelaxDiffuseSpecularConstants)), &rc, sizeof(RelaxDiffuseSpecularConstants));

    // Pass 0: Generate half-res linearized viewZ (necessary cause of GatherRed) + the packed guide the filter chain taps
    {
        graph.CreateTexture("relax_guide"_sid, TextureInfo{VK_FORMAT_R32G32_UINT, width, height, 1}, {std::nullopt}, true);

        auto& pass = graph.AddPass("[ReLAX] Generate ViewZ"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer("relax_constants"_sid);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.WriteStorageImage("relax_viewz"_sid);
        pass.WriteStorageImage("relax_guide"_sid);
        pass.Execute([pipelineManager, depth, gbufferOne, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxGenerateViewZPushConstant pc{
                .constants = graph.GetBufferAddress("relax_constants"_sid),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outViewZIndex = graph.GetStorageImageViewDescriptorIndex("relax_viewz"_sid),
                .outGuideIndex = graph.GetStorageImageViewDescriptorIndex("relax_guide"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_generate_viewz"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 1: Classify Tiles
    {
        graph.CreateTexture("relax_tiles"_sid, tilesInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass("[ReLAX] Classify Tiles"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer("relax_constants"_sid);
        pass.ReadSampledImage("relax_viewz"_sid);
        pass.WriteStorageImage("relax_tiles"_sid);
        pass.Execute([pipelineManager, tilesW, tilesH](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxClassifyTilesPushConstant pc{
                .constants = graph.GetBufferAddress("relax_constants"_sid),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex("relax_viewz"_sid),
                .tilesOutIndex = graph.GetStorageImageViewDescriptorIndex("relax_tiles"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_classify_tiles"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesW, tilesH, 1);
        });
    }


    // Pass 2: Prepass (optional spatial prefilter)
    if (bPrepass) {
        graph.CreateTexture("relax_spec_prepass"_sid, colorInfo, {std::nullopt}, true);
        graph.CreateTexture("relax_diff_prepass"_sid, colorInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass("[ReLAX] Prepass"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer("relax_constants"_sid);
        pass.ReadSampledImage("relax_tiles"_sid);
        pass.ReadSampledImage("relax_guide"_sid);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(diffInput);
        pass.WriteStorageImage("relax_spec_prepass"_sid);
        pass.WriteStorageImage("relax_diff_prepass"_sid);
        pass.Execute([pipelineManager, specInput, diffInput, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxPrepassPushConstant pc{
                .constants = graph.GetBufferAddress("relax_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("relax_tiles"_sid),
                .guideIndex = graph.GetSampledImageViewDescriptorIndex("relax_guide"_sid),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex("relax_spec_prepass"_sid),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex("relax_diff_prepass"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_prepass"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
        });
    }


    graph.CreateTexture("relax_spec_illum"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("relax_diff_illum"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("relax_spec_fast"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("relax_diff_fast"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("relax_spec_reproj_confidence"_sid, reprConfInfo, {std::nullopt}, true);


    // Pass 3: Temporal Accumulation
    {
        const StringID specIn = bPrepass ? "relax_spec_prepass"_sid : specInput;
        const StringID diffIn = bPrepass ? "relax_diff_prepass"_sid : diffInput;

        auto& pass = graph.AddPass("[ReLAX] Temporal Accumulation"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer("relax_constants"_sid);
        pass.ReadSampledImage("relax_tiles"_sid);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage("relax_guide"_sid);
        pass.ReadSampledImage(specIn);
        pass.ReadSampledImage(diffIn);
        if (graph.ResourceHasVersion("relax_spec_hist"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("relax_spec_hist"_sid, 1)); }
        if (graph.ResourceHasVersion("relax_diff_hist"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("relax_diff_hist"_sid, 1)); }
        if (graph.ResourceHasVersion("relax_spec_fast_hist"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("relax_spec_fast_hist"_sid, 1)); }
        if (graph.ResourceHasVersion("relax_diff_fast_hist"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("relax_diff_fast_hist"_sid, 1)); }
        if (graph.ResourceHasVersion("relax_history_length"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("relax_history_length"_sid, 1)); }
        if (graph.ResourceHasVersion("relax_spec_hit_dist"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("relax_spec_hit_dist"_sid, 1)); }
        if (graph.ResourceHasVersion("relax_prev_nr"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("relax_prev_nr"_sid, 1)); }
        if (graph.ResourceHasVersion("relax_viewz"_sid, 1)) {
            pass.ReadSampledImage(graph.ResourceVersionID("relax_viewz"_sid, 1));
        }
        else {
            pass.ReadSampledImage("relax_viewz"_sid);
        }
        if (graph.HasTexture("restir_confidence"_sid)) { pass.ReadSampledImage("restir_confidence"_sid); }
        if (graph.HasTexture(REFLECTION_HIT_DELTA_TARGET)) { pass.ReadSampledImage(REFLECTION_HIT_DELTA_TARGET); }
        if (graph.ResourceHasVersion(REFLECTION_HIT_DELTA_TARGET, 1)) { pass.ReadSampledImage(graph.ResourceVersionID(REFLECTION_HIT_DELTA_TARGET, 1)); }
        pass.WriteStorageImage("relax_spec_illum"_sid);
        pass.WriteStorageImage("relax_diff_illum"_sid);
        pass.WriteStorageImage("relax_spec_fast"_sid);
        pass.WriteStorageImage("relax_diff_fast"_sid);
        pass.WriteStorageImage("relax_history_length"_sid);
        pass.WriteStorageImage("relax_spec_hit_dist"_sid);
        pass.WriteStorageImage("relax_spec_reproj_confidence"_sid);
        pass.WriteStorageImage("relax_prev_nr"_sid);

        const bool hasHistory = graph.ResourceHasVersion("relax_spec_hist"_sid, 1);
        const StringID fallbackSpec = hasHistory ? graph.ResourceVersionID("relax_spec_hist"_sid, 1) : specIn;
        const StringID fallbackDiff = hasHistory ? graph.ResourceVersionID("relax_diff_hist"_sid, 1) : diffIn;
        const StringID fallbackSpecFast = graph.ResourceHasVersion("relax_spec_fast_hist"_sid, 1) ? graph.ResourceVersionID("relax_spec_fast_hist"_sid, 1) : specIn;
        const StringID fallbackDiffFast = graph.ResourceHasVersion("relax_diff_fast_hist"_sid, 1) ? graph.ResourceVersionID("relax_diff_fast_hist"_sid, 1) : diffIn;
        const StringID fallbackHistLen = graph.ResourceHasVersion("relax_history_length"_sid, 1) ? graph.ResourceVersionID("relax_history_length"_sid, 1) : "relax_history_length"_sid;
        const StringID fallbackSpecHitD = graph.ResourceHasVersion("relax_spec_hit_dist"_sid, 1) ? graph.ResourceVersionID("relax_spec_hit_dist"_sid, 1) : "relax_spec_hit_dist"_sid;
        const StringID fallbackPrevNR = graph.ResourceHasVersion("relax_prev_nr"_sid, 1) ? graph.ResourceVersionID("relax_prev_nr"_sid, 1) : "relax_prev_nr"_sid;
        const StringID fallbackViewZ = graph.ResourceHasVersion("relax_viewz"_sid, 1) ? graph.ResourceVersionID("relax_viewz"_sid, 1) : "relax_viewz"_sid;
        const bool hasHitDeltaHistory = graph.ResourceHasVersion(REFLECTION_HIT_DELTA_TARGET, 1);
        const StringID hitDeltaHistory = hasHitDeltaHistory ? graph.ResourceVersionID(REFLECTION_HIT_DELTA_TARGET, 1) : StringID{};

        pass.Execute([pipelineManager, specIn, diffIn, width, height, fallbackSpec, fallbackDiff, fallbackSpecFast, fallbackDiffFast, fallbackHistLen, fallbackSpecHitD, fallbackPrevNR, fallbackViewZ, hasHitDeltaHistory, hitDeltaHistory,
                gbufferOne](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxTemporalAccumulationPushConstant pc{
                .constants = graph.GetBufferAddress("relax_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("relax_tiles"_sid),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .guideIndex = graph.GetSampledImageViewDescriptorIndex("relax_guide"_sid),
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
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex("relax_history_length"_sid),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex("relax_spec_illum"_sid),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex("relax_diff_illum"_sid),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex("relax_spec_fast"_sid),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex("relax_diff_fast"_sid),
                .outSpecHitDistIndex = graph.GetStorageImageViewDescriptorIndex("relax_spec_hit_dist"_sid),
                .outSpecReprojConfidenceIndex = graph.GetStorageImageViewDescriptorIndex("relax_spec_reproj_confidence"_sid),
                .outPrevNRIndex = graph.GetStorageImageViewDescriptorIndex("relax_prev_nr"_sid),
                .confidenceIndex = graph.HasTexture("restir_confidence"_sid) ? graph.GetSampledImageViewDescriptorIndex("restir_confidence"_sid) : ~0u,
                .hitDeltaIndex = graph.HasTexture(REFLECTION_HIT_DELTA_TARGET) ? graph.GetSampledImageViewDescriptorIndex(REFLECTION_HIT_DELTA_TARGET) : ~0u,
                .hitDeltaHistoryIndex = hasHitDeltaHistory ? graph.GetSampledImageViewDescriptorIndex(hitDeltaHistory) : ~0u,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_temporal_accumulation"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 15) / 16, 1);
        });
    }


    graph.CreateTexture("relax_atrous_spec_0"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("relax_atrous_spec_1"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("relax_atrous_diff_0"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("relax_atrous_diff_1"_sid, colorInfo, {std::nullopt}, true);

    // Pass 4: History Fix. Filters the slow history but writes the responsive (fast) textures in place,
    // only at short-history pixels; clamping then promotes the fixed responsive value into the slow output.
    {
        auto& pass = graph.AddPass("[ReLAX] History Fix"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer("relax_constants"_sid);
        pass.ReadSampledImage("relax_tiles"_sid);
        pass.ReadSampledImage("relax_guide"_sid);
        pass.ReadSampledImage("relax_history_length"_sid);
        pass.ReadSampledImage("relax_spec_illum"_sid);
        pass.ReadSampledImage("relax_diff_illum"_sid);
        pass.ReadWriteImage("relax_spec_fast"_sid);
        pass.ReadWriteImage("relax_diff_fast"_sid);
        pass.Execute([pipelineManager, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxHistoryFixPushConstant pc{
                .constants = graph.GetBufferAddress("relax_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("relax_tiles"_sid),
                .guideIndex = graph.GetSampledImageViewDescriptorIndex("relax_guide"_sid),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex("relax_history_length"_sid),
                .specIndex = graph.GetSampledImageViewDescriptorIndex("relax_spec_illum"_sid),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex("relax_diff_illum"_sid),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex("relax_spec_fast"_sid),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex("relax_diff_fast"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_history_fix"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }


    // ----------------------------------------------------------------
    // Pass 5: History Clamping
    // ----------------------------------------------------------------
    {
        const StringID specNoisy = bPrepass ? "relax_spec_prepass"_sid : specInput;
        const StringID diffNoisy = bPrepass ? "relax_diff_prepass"_sid : diffInput;

        // With anti-firefly enabled the clamped slow output goes into the atrous scratch and anti-firefly produces relax_*_hist.
        // Without it, the clamping output is the carried slow history directly.
        const StringID clampSpecOut = params.enableAntiFirefly ? "relax_atrous_spec_0"_sid : "relax_spec_hist"_sid;
        const StringID clampDiffOut = params.enableAntiFirefly ? "relax_atrous_diff_0"_sid : "relax_diff_hist"_sid;

        auto& pass = graph.AddPass("[ReLAX] History Clamping"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer("relax_constants"_sid);
        pass.ReadSampledImage("relax_tiles"_sid);
        pass.ReadSampledImage("relax_viewz"_sid);
        pass.ReadWriteImage("relax_history_length"_sid);
        pass.ReadSampledImage("relax_spec_fast"_sid);
        pass.ReadSampledImage("relax_diff_fast"_sid);
        pass.ReadSampledImage("relax_spec_illum"_sid); // raw TA slow history
        pass.ReadSampledImage("relax_diff_illum"_sid);
        if (params.enableAntiFirefly) {
            pass.WriteStorageImage("relax_atrous_spec_0"_sid);
            pass.WriteStorageImage("relax_atrous_diff_0"_sid);
        } else {
            pass.WriteStorageImage("relax_spec_hist"_sid);
            pass.WriteStorageImage("relax_diff_hist"_sid);
        }
        pass.ReadSampledImage(specNoisy); // noisy preblur reference
        pass.ReadSampledImage(diffNoisy);
        pass.WriteStorageImage("relax_spec_fast_hist"_sid);
        pass.WriteStorageImage("relax_diff_fast_hist"_sid);
        pass.Execute([pipelineManager, specNoisy, diffNoisy, clampSpecOut, clampDiffOut, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxHistoryClampingPushConstant pc{
                .constants = graph.GetBufferAddress("relax_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("relax_tiles"_sid),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex("relax_viewz"_sid),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex("relax_history_length"_sid),
                .specFastIndex = graph.GetSampledImageViewDescriptorIndex("relax_spec_fast"_sid),
                .diffFastIndex = graph.GetSampledImageViewDescriptorIndex("relax_diff_fast"_sid),
                .specNoisyIndex = graph.GetSampledImageViewDescriptorIndex(specNoisy),
                .diffNoisyIndex = graph.GetSampledImageViewDescriptorIndex(diffNoisy),
                .specIndex = graph.GetSampledImageViewDescriptorIndex("relax_spec_illum"_sid),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex("relax_diff_illum"_sid),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(clampSpecOut),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(clampDiffOut),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex("relax_spec_fast_hist"_sid),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex("relax_diff_fast_hist"_sid),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex("relax_history_length"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_history_clamping"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 6: Anti-Firefly. RCRS filter from the clamped slow history (in the history-fix scratch) into relax_*_hist, so the firefly-suppressed result is what gets carried as next frame's history (matches NRD's Copy + Anti-Firefly writing into SPEC/DIFF_ILLUM_PREV).
    // Distinct input/output textures: the shared-memory preload reads a border from neighboring workgroups, so in-place filtering would race.
    if (params.enableAntiFirefly) {
        auto& pass = graph.AddPass("[ReLAX] Anti-Firefly"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer("relax_constants"_sid);
        pass.ReadSampledImage("relax_tiles"_sid);
        pass.ReadSampledImage("relax_viewz"_sid);
        pass.ReadSampledImage("relax_atrous_spec_0"_sid);
        pass.ReadSampledImage("relax_atrous_diff_0"_sid);
        pass.WriteStorageImage("relax_spec_hist"_sid);
        pass.WriteStorageImage("relax_diff_hist"_sid);

        pass.Execute([pipelineManager, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxAntiFireflyPushConstant pc{
                .constants = graph.GetBufferAddress("relax_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("relax_tiles"_sid),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex("relax_viewz"_sid),
                .specIndex = graph.GetSampledImageViewDescriptorIndex("relax_atrous_spec_0"_sid),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex("relax_atrous_diff_0"_sid),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex("relax_spec_hist"_sid),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex("relax_diff_hist"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_antifirefly"_sid);
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
        const StringID scratchSpec[2] = {"relax_atrous_spec_0"_sid, "relax_atrous_spec_1"_sid};
        const StringID scratchDiff[2] = {"relax_atrous_diff_0"_sid, "relax_atrous_diff_1"_sid};

        for (int32_t i = 0; i < iters; i++) {
            const bool isLast = (i == iters - 1);
            const StringID specIn = (i == 0) ? "relax_spec_hist"_sid : scratchSpec[(i - 1) & 1];
            const StringID diffIn = (i == 0) ? "relax_diff_hist"_sid : scratchDiff[(i - 1) & 1];
            const StringID specOut = isLast ? specInput : scratchSpec[i & 1];

            const StringID diffOut = (isLast && chromaIters == 0) ? diffInput : scratchDiff[i & 1];
            const uint32_t stepSize = 1u << static_cast<uint32_t>(i);

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReLAX] ATrous %d", i);

            auto& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
            pass.ReadBuffer("relax_constants"_sid);
            pass.ReadSampledImage("relax_tiles"_sid);
            pass.ReadSampledImage("relax_guide"_sid);
            pass.ReadSampledImage("relax_history_length"_sid);
            pass.ReadSampledImage("relax_spec_reproj_confidence"_sid);
            pass.ReadSampledImage(specIn);
            pass.ReadSampledImage(diffIn);
            pass.WriteStorageImage(specOut);
            pass.WriteStorageImage(diffOut);

            pass.Execute([pipelineManager,
                    specIn, diffIn, specOut, diffOut, stepSize, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    RelaxAtrousPushConstant pc{
                        .constants = graph.GetBufferAddress("relax_constants"_sid),
                        .tilesIndex = graph.GetSampledImageViewDescriptorIndex("relax_tiles"_sid),
                        .guideIndex = graph.GetSampledImageViewDescriptorIndex("relax_guide"_sid),
                        .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex("relax_history_length"_sid),
                        .specVarIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                        .diffVarIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                        .specReprojConfidenceIndex = graph.GetSampledImageViewDescriptorIndex("relax_spec_reproj_confidence"_sid),
                        .specIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                        .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                        .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specOut),
                        .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffOut),
                        .stepSize = stepSize,
                    };
                    const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_atrous"_sid);
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
            pass.ReadBuffer("relax_constants"_sid);
            pass.ReadSampledImage("relax_tiles"_sid);
            pass.ReadSampledImage("relax_guide"_sid);
            pass.ReadSampledImage("relax_history_length"_sid);
            pass.ReadSampledImage(diffIn);
            pass.WriteStorageImage(diffOut);

            pass.Execute([pipelineManager, diffIn, diffOut, stepSize, width, height, chromaLumaPower = params.chromaLumaPower](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                RelaxAtrousPushConstant pc{
                    .constants = graph.GetBufferAddress("relax_constants"_sid),
                    .tilesIndex = graph.GetSampledImageViewDescriptorIndex("relax_tiles"_sid),
                    .guideIndex = graph.GetSampledImageViewDescriptorIndex("relax_guide"_sid),
                    .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex("relax_history_length"_sid),
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
                const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_atrous_chroma"_sid);
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
        const StringID reflectionTarget = REFLECTION_SPEC_NOISY_TARGET;
        const bool bReflectionMerged = reflectionConfig.bMergedDenoise && reflectionRoughnessMax >= 0.0f && graph.HasTexture(REFLECTION_SPEC_NOISY_TARGET);
        const bool bReflection = !bReflectionMerged && reflectionRoughnessMax >= 0.0f && graph.HasTexture(reflectionTarget);

        const StringID shadows = targets.shadows;

        auto& pass = graph.AddPass("[ReLAX] Remodulate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.ReadBuffer(LIGHT_DATA_BUFFER);
        pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
        if (graph.HasBuffer("world_grid_probe_grid"_sid)) { pass.ReadBuffer("world_grid_probe_grid"_sid); }
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
        pass.Execute([pipelineManager, diffInput, specInput, gbufferOne, gbufferTwo, depth, noisyInput, width, height, remodulateOutputMode, skyboxIndex, iblIntensity, indirectIntensity = viewFamily.indirectIntensity, bDDGI, shadows, bReflection, bReflectionMerged, reflectionRoughnessMax, reflectionTarget, bGIGather, giGatherMode, reflectionProbeCount, bProbeBrute](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .indirectIntensity = indirectIntensity,
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
                .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer("world_grid_probe_grid"_sid)) ? graph.GetBufferAddress("world_grid_probe_grid"_sid) : 0,
                .bReflectionMerged = bReflectionMerged ? 1u : 0u,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("restir_remodulate"_sid);
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
    ZoneScoped;
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

    const TextureInfo colorInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1};
    const TextureInfo histLenInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo hitDistInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo tilesInfo{VK_FORMAT_R8_UNORM, tilesW, tilesH, 1};
    const TextureInfo viewZInfo{VK_FORMAT_R32_SFLOAT, width, height, 1};
    const TextureInfo data1Info{VK_FORMAT_R8G8_UNORM, width, height, 1};
    const TextureInfo data2Info{VK_FORMAT_R32_UINT, width, height, 1};

    // History rings must be declared before anything queries them below.
    graph.CreateVersionedTexture("reblur_viewz"_sid, viewZInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("reblur_spec_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("reblur_diff_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("reblur_spec_fast_fixed"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("reblur_diff_fast_fixed"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("reblur_internal_data"_sid, data2Info, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("reblur_spec_hit_dist"_sid, hitDistInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("reblur_prev_nr"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("reblur_spec_luma_stab"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CreateVersionedTexture("reblur_diff_luma_stab"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);

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

    const bool bFirstFrame = !graph.ResourceHasVersion("reblur_spec_hist"_sid, 1);

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
    memcpy(graph.OpenHostBuffer("reblur_constants"_sid, sizeof(ReblurDiffuseSpecularConstants)), &rc, sizeof(ReblurDiffuseSpecularConstants));

    // Pass 0: Generate viewZ
    {
        auto& pass = graph.AddPass("[ReBLUR] Generate ViewZ"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer("reblur_constants"_sid);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.WriteStorageImage("reblur_viewz"_sid);
        pass.Execute([pipelineManager, depth, gbufferOne, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurGenerateViewZPushConstant pc{
                .constants = graph.GetBufferAddress("reblur_constants"_sid),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outViewZIndex = graph.GetStorageImageViewDescriptorIndex("reblur_viewz"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_generate_viewz"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 1: Classify tiles
    {
        graph.CreateTexture("reblur_tiles"_sid, tilesInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass("[ReBLUR] Classify Tiles"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer("reblur_constants"_sid);
        pass.ReadSampledImage(depth);
        pass.WriteStorageImage("reblur_tiles"_sid);
        pass.Execute([pipelineManager, depth, tilesW, tilesH](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurClassifyTilesPushConstant pc{
                .constants = graph.GetBufferAddress("reblur_constants"_sid),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .tilesOutIndex = graph.GetStorageImageViewDescriptorIndex("reblur_tiles"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_classify_tiles"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesW, tilesH, 1);
        });
    }

    // Pass 2: Front-end pack (raw RGB + hitDist -> YCoCg + hitDist)
    graph.CreateTexture("reblur_spec_packed"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("reblur_diff_packed"_sid, colorInfo, {std::nullopt}, true);
    {
        auto& pass = graph.AddPass("[ReBLUR] Pack"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer("reblur_constants"_sid);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(diffInput);
        pass.WriteStorageImage("reblur_spec_packed"_sid);
        pass.WriteStorageImage("reblur_diff_packed"_sid);
        pass.Execute([pipelineManager, depth, gbufferOne, specInput, diffInput, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurPackPushConstant pc{
                .constants = graph.GetBufferAddress("reblur_constants"_sid),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex("reblur_spec_packed"_sid),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex("reblur_diff_packed"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_pack"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 3: Prepass (optional; forced on for checkerboard hole resolve)
    if (bPrepass) {
        graph.CreateTexture("reblur_spec_prepass"_sid, colorInfo, {std::nullopt}, true);
        graph.CreateTexture("reblur_diff_prepass"_sid, colorInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass("[ReBLUR] Prepass"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer("reblur_constants"_sid);
        pass.ReadSampledImage("reblur_tiles"_sid);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage("reblur_spec_packed"_sid);
        pass.ReadSampledImage("reblur_diff_packed"_sid);
        pass.WriteStorageImage("reblur_spec_prepass"_sid);
        pass.WriteStorageImage("reblur_diff_prepass"_sid);
        pass.Execute([pipelineManager, depth, gbufferOne, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurPrepassPushConstant pc{
                .constants = graph.GetBufferAddress("reblur_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("reblur_tiles"_sid),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex("reblur_spec_packed"_sid),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex("reblur_diff_packed"_sid),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex("reblur_spec_prepass"_sid),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex("reblur_diff_prepass"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_prepass"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
        });
    }

    const StringID specIn = bPrepass ? "reblur_spec_prepass"_sid : "reblur_spec_packed"_sid;
    const StringID diffIn = bPrepass ? "reblur_diff_prepass"_sid : "reblur_diff_packed"_sid;

    graph.CreateTexture("reblur_spec_accum"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("reblur_diff_accum"_sid, colorInfo, {std::nullopt}, true);
    // Fast (responsive) history is NRD-faithful single-channel luma (R16F), not RGBA.
    graph.CreateTexture("reblur_spec_fast"_sid, histLenInfo, {std::nullopt}, true);
    graph.CreateTexture("reblur_diff_fast"_sid, histLenInfo, {std::nullopt}, true);
    // DATA1 = per-lobe accum frames (RG8), DATA2 = occlusion bits + curvature + vha (R32U);
    // internal data = carried per-lobe accum speeds written by stabilization with antilag feedback.
    graph.CreateTexture("reblur_data1"_sid, data1Info, {std::nullopt}, true);
    graph.CreateTexture("reblur_data2"_sid, data2Info, {std::nullopt}, true);
    graph.CreateTexture("reblur_spec_hfix"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("reblur_diff_hfix"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("reblur_spec_blur"_sid, colorInfo, {std::nullopt}, true);
    graph.CreateTexture("reblur_diff_blur"_sid, colorInfo, {std::nullopt}, true);

    // Pass 4: Temporal accumulation
    {
        auto& pass = graph.AddPass("[ReBLUR] Temporal Accumulation"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer("reblur_constants"_sid);
        pass.ReadSampledImage("reblur_tiles"_sid);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(specIn);
        pass.ReadSampledImage(diffIn);
        if (graph.ResourceHasVersion("reblur_spec_hist"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_spec_hist"_sid, 1)); }
        if (graph.ResourceHasVersion("reblur_diff_hist"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_diff_hist"_sid, 1)); }
        if (graph.ResourceHasVersion("reblur_spec_fast_fixed"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_spec_fast_fixed"_sid, 1)); }
        if (graph.ResourceHasVersion("reblur_diff_fast_fixed"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_diff_fast_fixed"_sid, 1)); }
        if (graph.ResourceHasVersion("reblur_internal_data"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_internal_data"_sid, 1)); }
        if (graph.ResourceHasVersion("reblur_spec_hit_dist"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_spec_hit_dist"_sid, 1)); }
        if (graph.ResourceHasVersion("reblur_prev_nr"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_prev_nr"_sid, 1)); }
        if (graph.ResourceHasVersion("reblur_viewz"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_viewz"_sid, 1)); }
        else { pass.ReadSampledImage("reblur_viewz"_sid); }
        if (graph.HasTexture("restir_confidence"_sid)) { pass.ReadSampledImage("restir_confidence"_sid); }
        pass.WriteStorageImage("reblur_spec_accum"_sid);
        pass.WriteStorageImage("reblur_diff_accum"_sid);
        pass.WriteStorageImage("reblur_spec_fast"_sid);
        pass.WriteStorageImage("reblur_diff_fast"_sid);
        pass.WriteStorageImage("reblur_data1"_sid);
        pass.WriteStorageImage("reblur_data2"_sid);
        pass.WriteStorageImage("reblur_spec_hit_dist"_sid);
        pass.WriteStorageImage("reblur_prev_nr"_sid);

        const bool hasHistory = graph.ResourceHasVersion("reblur_spec_hist"_sid, 1);
        const StringID fallbackSpec = hasHistory ? graph.ResourceVersionID("reblur_spec_hist"_sid, 1) : specIn;
        const StringID fallbackDiff = hasHistory ? graph.ResourceVersionID("reblur_diff_hist"_sid, 1) : diffIn;
        // First-frame fallbacks must be format-compatible sources (values unused under gResetHistory).
        const StringID fallbackSpecFast = graph.ResourceHasVersion("reblur_spec_fast_fixed"_sid, 1) ? graph.ResourceVersionID("reblur_spec_fast_fixed"_sid, 1) : "reblur_spec_hit_dist"_sid;
        const StringID fallbackDiffFast = graph.ResourceHasVersion("reblur_diff_fast_fixed"_sid, 1) ? graph.ResourceVersionID("reblur_diff_fast_fixed"_sid, 1) : "reblur_spec_hit_dist"_sid;
        const StringID fallbackInternalData = graph.ResourceHasVersion("reblur_internal_data"_sid, 1) ? graph.ResourceVersionID("reblur_internal_data"_sid, 1) : "reblur_data2"_sid;
        const StringID fallbackSpecHitD = graph.ResourceHasVersion("reblur_spec_hit_dist"_sid, 1) ? graph.ResourceVersionID("reblur_spec_hit_dist"_sid, 1) : "reblur_spec_hit_dist"_sid;
        const StringID fallbackPrevNR = graph.ResourceHasVersion("reblur_prev_nr"_sid, 1) ? graph.ResourceVersionID("reblur_prev_nr"_sid, 1) : "reblur_prev_nr"_sid;
        const StringID fallbackViewZ = graph.ResourceHasVersion("reblur_viewz"_sid, 1) ? graph.ResourceVersionID("reblur_viewz"_sid, 1) : "reblur_viewz"_sid;

        pass.Execute([pipelineManager, gbufferOne, depth, specIn, diffIn, width, height, fallbackSpec, fallbackDiff, fallbackSpecFast, fallbackDiffFast, fallbackInternalData, fallbackSpecHitD, fallbackPrevNR,
                fallbackViewZ](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurTemporalAccumulationPushConstant pc{
                .constants = graph.GetBufferAddress("reblur_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("reblur_tiles"_sid),
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
                .outData1Index = graph.GetStorageImageViewDescriptorIndex("reblur_data1"_sid),
                .outData2Index = graph.GetStorageImageViewDescriptorIndex("reblur_data2"_sid),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex("reblur_spec_accum"_sid),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex("reblur_diff_accum"_sid),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex("reblur_spec_fast"_sid),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex("reblur_diff_fast"_sid),
                .outSpecHitDistIndex = graph.GetStorageImageViewDescriptorIndex("reblur_spec_hit_dist"_sid),
                .outPrevNRIndex = graph.GetStorageImageViewDescriptorIndex("reblur_prev_nr"_sid),
                .confidenceIndex = graph.HasTexture("restir_confidence"_sid) ? graph.GetSampledImageViewDescriptorIndex("restir_confidence"_sid) : ~0u,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_temporal_accumulation"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 15) / 16, 1);
        });
    }

    // Pass 5: History fix
    {
        auto& pass = graph.AddPass("[ReBLUR] History Fix"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer("reblur_constants"_sid);
        pass.ReadSampledImage("reblur_tiles"_sid);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage("reblur_data1"_sid);
        pass.ReadSampledImage("reblur_spec_accum"_sid);
        pass.ReadSampledImage("reblur_diff_accum"_sid);
        pass.ReadSampledImage("reblur_spec_fast"_sid);
        pass.ReadSampledImage("reblur_diff_fast"_sid);
        pass.WriteStorageImage("reblur_spec_hfix"_sid);
        pass.WriteStorageImage("reblur_diff_hfix"_sid);
        pass.WriteStorageImage("reblur_spec_fast_fixed"_sid);
        pass.WriteStorageImage("reblur_diff_fast_fixed"_sid);
        pass.Execute([pipelineManager, gbufferOne, depth, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurHistoryFixPushConstant pc{
                .constants = graph.GetBufferAddress("reblur_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("reblur_tiles"_sid),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .data1Index = graph.GetSampledImageViewDescriptorIndex("reblur_data1"_sid),
                .specIndex = graph.GetSampledImageViewDescriptorIndex("reblur_spec_accum"_sid),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex("reblur_diff_accum"_sid),
                .specFastIndex = graph.GetSampledImageViewDescriptorIndex("reblur_spec_fast"_sid),
                .diffFastIndex = graph.GetSampledImageViewDescriptorIndex("reblur_diff_fast"_sid),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex("reblur_spec_hfix"_sid),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex("reblur_diff_hfix"_sid),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex("reblur_spec_fast_fixed"_sid),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex("reblur_diff_fast_fixed"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_history_fix"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Passes 6 & 7: Blur and Post-Blur (one pipeline, isPostBlur toggles radius). Post-blur output is the carried slow history.
    auto addBlur = [&](StringID passName, StringID specSrc, StringID diffSrc, StringID specDst, StringID diffDst, uint32_t isPostBlur) {
        auto& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer("reblur_constants"_sid);
        pass.ReadSampledImage("reblur_tiles"_sid);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage("reblur_data1"_sid);
        pass.ReadSampledImage(specSrc);
        pass.ReadSampledImage(diffSrc);
        pass.WriteStorageImage(specDst);
        pass.WriteStorageImage(diffDst);
        pass.Execute([pipelineManager, gbufferOne, depth, specSrc, diffSrc, specDst, diffDst, isPostBlur, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurBlurPushConstant pc{
                .constants = graph.GetBufferAddress("reblur_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("reblur_tiles"_sid),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .data1Index = graph.GetSampledImageViewDescriptorIndex("reblur_data1"_sid),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(specSrc),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffSrc),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specDst),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffDst),
                .isPostBlur = isPostBlur,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_blur"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    };
    addBlur("[ReBLUR] Blur"_sid, "reblur_spec_hfix"_sid, "reblur_diff_hfix"_sid, "reblur_spec_blur"_sid, "reblur_diff_blur"_sid, 0u);
    addBlur("[ReBLUR] Post-Blur"_sid, "reblur_spec_blur"_sid, "reblur_diff_blur"_sid, "reblur_spec_hist"_sid, "reblur_diff_hist"_sid, 1u);

    const int32_t chromaIters = params.bChromaAtrous ? glm::clamp(params.chromaAtrousIterations, 1, 4) : 0;
    const StringID stabilizationDiffOut = chromaIters > 0 ? "reblur_diff_blur"_sid : diffInput;

    // Pass 8: Temporal stabilization (luma-only stabilized ping-pong, surface + virtual motion via the DATA2
    // occlusion bits, antilag feedback written into the carried internal data, final RGB into intermediates)
    {
        auto& pass = graph.AddPass("[ReBLUR] Temporal Stabilization"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer("reblur_constants"_sid);
        pass.ReadSampledImage("reblur_tiles"_sid);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage("reblur_data1"_sid);
        pass.ReadSampledImage("reblur_data2"_sid);
        pass.ReadSampledImage("reblur_spec_hit_dist"_sid);
        pass.ReadSampledImage("reblur_spec_hist"_sid);
        pass.ReadSampledImage("reblur_diff_hist"_sid);
        if (graph.ResourceHasVersion("reblur_spec_luma_stab"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_spec_luma_stab"_sid, 1)); }
        if (graph.ResourceHasVersion("reblur_diff_luma_stab"_sid, 1)) { pass.ReadSampledImage(graph.ResourceVersionID("reblur_diff_luma_stab"_sid, 1)); }
        pass.WriteStorageImage("reblur_spec_luma_stab"_sid);
        pass.WriteStorageImage("reblur_diff_luma_stab"_sid);
        pass.WriteStorageImage("reblur_internal_data"_sid);
        pass.WriteStorageImage(specInput);
        pass.WriteStorageImage(stabilizationDiffOut);
        const StringID fallbackSpecStab = graph.ResourceHasVersion("reblur_spec_luma_stab"_sid, 1) ? graph.ResourceVersionID("reblur_spec_luma_stab"_sid, 1) : "reblur_spec_hit_dist"_sid;
        const StringID fallbackDiffStab = graph.ResourceHasVersion("reblur_diff_luma_stab"_sid, 1) ? graph.ResourceVersionID("reblur_diff_luma_stab"_sid, 1) : "reblur_spec_hit_dist"_sid;

        pass.Execute([pipelineManager, gbufferOne, depth, specInput, stabilizationDiffOut, width, height, fallbackSpecStab, fallbackDiffStab](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurStabilizationPushConstant pc{
                .constants = graph.GetBufferAddress("reblur_constants"_sid),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex("reblur_tiles"_sid),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .specIndex = graph.GetSampledImageViewDescriptorIndex("reblur_spec_hist"_sid),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex("reblur_diff_hist"_sid),
                .data1Index = graph.GetSampledImageViewDescriptorIndex("reblur_data1"_sid),
                .data2Index = graph.GetSampledImageViewDescriptorIndex("reblur_data2"_sid),
                .specHitDistIndex = graph.GetSampledImageViewDescriptorIndex("reblur_spec_hit_dist"_sid),
                .prevSpecLumaStabIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecStab),
                .prevDiffLumaStabIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiffStab),
                .outSpecLumaStabIndex = graph.GetStorageImageViewDescriptorIndex("reblur_spec_luma_stab"_sid),
                .outDiffLumaStabIndex = graph.GetStorageImageViewDescriptorIndex("reblur_diff_luma_stab"_sid),
                .outSpecFinalIndex = graph.GetStorageImageViewDescriptorIndex(specInput),
                .outDiffFinalIndex = graph.GetStorageImageViewDescriptorIndex(stabilizationDiffOut),
                .outInternalDataIndex = graph.GetStorageImageViewDescriptorIndex("reblur_internal_data"_sid),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_stabilization"_sid);
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
            const StringID inTex = (c & 1) ? "reblur_diff_hfix"_sid : "reblur_diff_blur"_sid;
            const StringID outTex = isLastChroma ? diffInput : ((c & 1) ? "reblur_diff_blur"_sid : "reblur_diff_hfix"_sid);
            const uint32_t stepSize = chromaStrides[c];

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReBLUR] Chroma %d", c);

            auto& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
            pass.ReadBuffer("reblur_constants"_sid);
            pass.ReadSampledImage("reblur_tiles"_sid);
            pass.ReadSampledImage(gbufferOne);
            pass.ReadSampledImage(depth);
            pass.ReadSampledImage("reblur_data1"_sid);
            pass.ReadSampledImage(inTex);
            pass.WriteStorageImage(outTex);

            pass.Execute([pipelineManager, gbufferOne, depth, inTex, outTex, stepSize, width, height, chromaLumaPower = params.chromaLumaPower](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                ReblurChromaPushConstant pc{
                    .constants = graph.GetBufferAddress("reblur_constants"_sid),
                    .tilesIndex = graph.GetSampledImageViewDescriptorIndex("reblur_tiles"_sid),
                    .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .data1Index = graph.GetSampledImageViewDescriptorIndex("reblur_data1"_sid),
                    .diffIndex = graph.GetSampledImageViewDescriptorIndex(inTex),
                    .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(outTex),
                    .stepSize = stepSize,
                    .chromaLumaPower = chromaLumaPower,
                };
                const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_chroma"_sid);
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
        const StringID reflectionTarget = REFLECTION_SPEC_NOISY_TARGET;
        const bool bReflectionMerged = reflectionConfig.bMergedDenoise && reflectionRoughnessMax >= 0.0f && graph.HasTexture(REFLECTION_SPEC_NOISY_TARGET);
        const bool bReflection = !bReflectionMerged && reflectionRoughnessMax >= 0.0f && graph.HasTexture(reflectionTarget);

        const StringID shadows = targets.shadows;

        auto& pass = graph.AddPass("[ReBLUR] Remodulate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.ReadBuffer(LIGHT_DATA_BUFFER);
        pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
        if (graph.HasBuffer("world_grid_probe_grid"_sid)) { pass.ReadBuffer("world_grid_probe_grid"_sid); }
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
        pass.Execute([pipelineManager, diffInput, specInput, gbufferOne, gbufferTwo, depth, noisyInput, width, height, remodulateOutputMode, skyboxIndex, iblIntensity, indirectIntensity = viewFamily.indirectIntensity, bDDGI, shadows, bReflection, bReflectionMerged, reflectionRoughnessMax, reflectionTarget, bGIGather, giGatherMode, reflectionProbeCount, bProbeBrute](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
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
                .indirectIntensity = indirectIntensity,
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
                .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer("world_grid_probe_grid"_sid)) ? graph.GetBufferAddress("world_grid_probe_grid"_sid) : 0,
                .bReflectionMerged = bReflectionMerged ? 1u : 0u,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("restir_remodulate"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }
}
} // Render