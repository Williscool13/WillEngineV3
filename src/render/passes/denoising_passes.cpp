//
// Created by William on 2026-06-03.
//

#include "render/passes/denoising_passes.h"

#include <tracy/Tracy.hpp>

#include "render/passes/ddgi_passes.h"
#include "render/passes/final_gather_passes.h"
#include "render/passes/reflection_passes.h"
#include "render/passes/shadow_passes.h"
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
static constexpr float RELAX_MAX_ACCUM_FRAME_NUM = 255.0f;
static constexpr float REBLUR_MAX_ACCUM_FRAME_NUM = 63.0f;

RDGTexture SetupRELAXDenoiser(RenderGraph& graph,
                        PipelineManager* pipelineManager,
                        const Core::ViewFamily& viewFamily,
                        Core::Extent2D renderExtent,
                        const RenderTargets& targets,
                        const SceneResources& scene,
                        const ReSTIRFrame& restir,
                        const ReflectionFrame& reflection,
                        const FinalGatherFrame& finalGather,
                        const DDGIFrame& ddgi,
                        const WorldGridFrame& worldGrid,
                        const Core::RELAXParams& params,
                        uint64_t frameNumber,
                        uint32_t remodulateOutputMode,
                        float iblIntensity,
                        uint32_t activeCheckerboardField,
                        float checkerboardResolveAccumSpeed,
                        bool bDDGIApply,
                        const Core::ReflectionConfiguration& reflectionConfig,
                        uint32_t giGatherMode,
                        float historyExposureRatio)
{
    ZoneScoped;
    const bool bCheckerboard = activeCheckerboardField != 0u;
    // NRD RELAX resolves checkerboard inside the prepass
    const bool bPrepass = params.enablePrepass || bCheckerboard;
    const uint32_t width = renderExtent.width;
    const uint32_t height = renderExtent.height;
    const uint32_t tilesW = (width + 15) / 16;
    const uint32_t tilesH = (height + 15) / 16;

    const RDGTexture gbufferOne = targets.gbufferOne;
    const RDGTexture depth = targets.depthCopy;
    const RDGTexture specInput = targets.intermediateTwo;
    const RDGTexture diffInput = targets.intermediateOne;
    const RDGTexture noisyInput = targets.colorOutput;
    const RDGTexture confidence = restir.confidence;
    const RDGTexture hitDelta = reflection.hitDelta;

    // Declare transient textures
    const TextureInfo colorInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1};
    const TextureInfo histLenInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo hitDistInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo reprConfInfo{VK_FORMAT_R8_UNORM, width, height, 1};
    const TextureInfo tilesInfo{VK_FORMAT_R8_UNORM, tilesW, tilesH, 1};
    const TextureInfo viewZInfo{VK_FORMAT_R32_SFLOAT, width, height, 1};

    // History rings must be declared before anything queries them below.
    const RDGTextureRing viewZRing = graph.CreateVersionedTexture("relax_viewz"_sid, viewZInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing specHistRing = graph.CreateVersionedTexture("relax_spec_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing diffHistRing = graph.CreateVersionedTexture("relax_diff_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing specFastHistRing = graph.CreateVersionedTexture("relax_spec_fast_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing diffFastHistRing = graph.CreateVersionedTexture("relax_diff_fast_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing historyLengthRing = graph.CreateVersionedTexture("relax_history_length"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing specHitDistRing = graph.CreateVersionedTexture("relax_spec_hit_dist"_sid, hitDistInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing prevNRRing = graph.CreateVersionedTexture("relax_prev_nr"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);

    const RDGTexture viewZ = viewZRing.Current();
    const RDGTexture specHist = specHistRing.Current();
    const RDGTexture diffHist = diffHistRing.Current();
    const RDGTexture specFastHist = specFastHistRing.Current();
    const RDGTexture diffFastHist = diffFastHistRing.Current();
    const RDGTexture historyLength = historyLengthRing.Current();
    const RDGTexture specHitDist = specHitDistRing.Current();
    const RDGTexture prevNR = prevNRRing.Current();

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

    const bool bFirstFrame = !specHistRing.Version(1).IsValid();

    RelaxDiffuseSpecularConstants rc{};

    rc.gWorldToClip = proj * rotView;
    rc.gWorldToClipPrev = prevProj * worldToViewPrev;

    // NRD expects positive viewZ from gWorldToViewPrev; negate the z-output row.
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

    rc.gSpecMaxAccumulatedFrameNum = glm::min(params.specMaxAccumFrames * params.framerateScale, RELAX_MAX_ACCUM_FRAME_NUM);
    rc.gSpecMaxFastAccumulatedFrameNum = glm::min(params.specMaxFastAccumFrames * params.framerateScale, RELAX_MAX_ACCUM_FRAME_NUM);
    rc.gDiffMaxAccumulatedFrameNum = glm::min(params.diffMaxAccumFrames * params.framerateScale, RELAX_MAX_ACCUM_FRAME_NUM);
    rc.gDiffMaxFastAccumulatedFrameNum = glm::min(params.diffMaxFastAccumFrames * params.framerateScale, RELAX_MAX_ACCUM_FRAME_NUM);
    const float jitterDelta = ComputeRelaxJitterDelta(viewFamily.aaConfig.mode, frameNumber, viewFamily.resolutionScale);
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
    rc.gHistoryExposureRatio = historyExposureRatio;
    rc.gMinHitDistanceWeight = params.minHitDistanceWeight;
    rc.gRoughnessEdgeStoppingEnabled = params.roughnessEdgeStoppingEnabled ? 1u : 0u;
    rc.gFrameIndex = static_cast<uint32_t>(frameNumber);
    rc.gResetHistory = bFirstFrame ? 1u : 0u;
    // Resolve writes diffuse and specular for the same active pixels; both signals live on field 0.
    rc.gDiffCheckerboard = bCheckerboard ? 0u : 2u;
    rc.gSpecCheckerboard = bCheckerboard ? 0u : 2u;
    rc.gCheckerboardResolveAccumSpeed = bCheckerboard ? checkerboardResolveAccumSpeed : 0.0f;

    // Upload constants buffer
    const HostBufferMapping constantsMapping = graph.OpenHostBuffer("relax_constants"_sid, sizeof(RelaxDiffuseSpecularConstants));
    memcpy(constantsMapping.data, &rc, sizeof(RelaxDiffuseSpecularConstants));
    const RDGBuffer constants = constantsMapping.buffer;

    // Pass 0: Generate half-res linearized viewZ (necessary cause of GatherRed) + the packed guide the filter chain taps
    const RDGTexture guide = graph.CreateTexture("relax_guide"_sid, TextureInfo{VK_FORMAT_R32G32_UINT, width, height, 1}, {std::nullopt}, true);
    {
        auto& pass = graph.AddPass("[ReLAX] Generate ViewZ"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.WriteStorageImage(viewZ);
        pass.WriteStorageImage(guide);
        pass.Execute([pipelineManager, constants, depth, gbufferOne, viewZ, guide, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxGenerateViewZPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outViewZIndex = graph.GetStorageImageViewDescriptorIndex(viewZ),
                .outGuideIndex = graph.GetStorageImageViewDescriptorIndex(guide),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_generate_viewz"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 1: Classify Tiles
    const RDGTexture tiles = graph.CreateTexture("relax_tiles"_sid, tilesInfo, {std::nullopt}, true);
    {
        auto& pass = graph.AddPass("[ReLAX] Classify Tiles"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(viewZ);
        pass.WriteStorageImage(tiles);
        pass.Execute([pipelineManager, constants, viewZ, tiles, tilesW, tilesH](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxClassifyTilesPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(viewZ),
                .tilesOutIndex = graph.GetStorageImageViewDescriptorIndex(tiles),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_classify_tiles"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesW, tilesH, 1);
        });
    }


    // Pass 2: Prepass (optional spatial prefilter)
    RDGTexture specPrepass{};
    RDGTexture diffPrepass{};
    if (bPrepass) {
        specPrepass = graph.CreateTexture("relax_spec_prepass"_sid, colorInfo, {std::nullopt}, true);
        diffPrepass = graph.CreateTexture("relax_diff_prepass"_sid, colorInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass("[ReLAX] Prepass"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(guide);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(diffInput);
        pass.WriteStorageImage(specPrepass);
        pass.WriteStorageImage(diffPrepass);
        pass.Execute([pipelineManager, constants, tiles, guide, specInput, diffInput, specPrepass, diffPrepass, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxPrepassPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .guideIndex = graph.GetSampledImageViewDescriptorIndex(guide),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex(specPrepass),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex(diffPrepass),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_prepass"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
        });
    }


    const RDGTexture specIllum = graph.CreateTexture("relax_spec_illum"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture diffIllum = graph.CreateTexture("relax_diff_illum"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture specFast = graph.CreateTexture("relax_spec_fast"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture diffFast = graph.CreateTexture("relax_diff_fast"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture specReprojConfidence = graph.CreateTexture("relax_spec_reproj_confidence"_sid, reprConfInfo, {std::nullopt}, true);

    const bool bVirtualMotion = (viewFamily.postProcessConfig.bMotionBlurEnabled || viewFamily.aaConfig.mode == Core::AntiAliasingMode::FSR2) && reflectionConfig.bEnabled;
    RDGTexture virtualMotion{};
    if (bVirtualMotion) {
        virtualMotion = graph.CreateTexture("reflection_virtual_motion"_sid,TextureInfo{VK_FORMAT_R16G16_SFLOAT, width, height, 1}, {std::nullopt}, true);
    }


    // Pass 3: Temporal Accumulation
    {
        const RDGTexture specIn = bPrepass ? specPrepass : specInput;
        const RDGTexture diffIn = bPrepass ? diffPrepass : diffInput;

        auto& pass = graph.AddPass("[ReLAX] Temporal Accumulation"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(guide);
        pass.ReadSampledImage(specIn);
        pass.ReadSampledImage(diffIn);
        if (specHistRing.Version(1).IsValid()) { pass.ReadSampledImage(specHistRing.Version(1)); }
        if (diffHistRing.Version(1).IsValid()) { pass.ReadSampledImage(diffHistRing.Version(1)); }
        if (specFastHistRing.Version(1).IsValid()) { pass.ReadSampledImage(specFastHistRing.Version(1)); }
        if (diffFastHistRing.Version(1).IsValid()) { pass.ReadSampledImage(diffFastHistRing.Version(1)); }
        if (historyLengthRing.Version(1).IsValid()) { pass.ReadSampledImage(historyLengthRing.Version(1)); }
        if (specHitDistRing.Version(1).IsValid()) { pass.ReadSampledImage(specHitDistRing.Version(1)); }
        if (prevNRRing.Version(1).IsValid()) { pass.ReadSampledImage(prevNRRing.Version(1)); }
        if (viewZRing.Version(1).IsValid()) {
            pass.ReadSampledImage(viewZRing.Version(1));
        }
        else {
            pass.ReadSampledImage(viewZ);
        }
        if (confidence.IsValid()) { pass.ReadSampledImage(confidence); }
        if (hitDelta.IsValid()) { pass.ReadSampledImage(hitDelta); }
        if (hitDelta.IsValid() && reflection.hitDeltaHistory.IsValid()) { pass.ReadSampledImage(reflection.hitDeltaHistory); }
        pass.WriteStorageImage(specIllum);
        pass.WriteStorageImage(diffIllum);
        pass.WriteStorageImage(specFast);
        pass.WriteStorageImage(diffFast);
        pass.WriteStorageImage(historyLength);
        pass.WriteStorageImage(specHitDist);
        pass.WriteStorageImage(specReprojConfidence);
        pass.WriteStorageImage(prevNR);
        if (bVirtualMotion) { pass.WriteStorageImage(virtualMotion); }

        const bool hasHistory = specHistRing.Version(1).IsValid();
        const RDGTexture fallbackSpec = hasHistory ? specHistRing.Version(1) : specIn;
        const RDGTexture fallbackDiff = hasHistory ? diffHistRing.Version(1) : diffIn;
        const RDGTexture fallbackSpecFast = specFastHistRing.Version(1).IsValid() ? specFastHistRing.Version(1) : specIn;
        const RDGTexture fallbackDiffFast = diffFastHistRing.Version(1).IsValid() ? diffFastHistRing.Version(1) : diffIn;
        const RDGTexture fallbackHistLen = historyLengthRing.Version(1).IsValid() ? historyLengthRing.Version(1) : historyLength;
        const RDGTexture fallbackSpecHitD = specHitDistRing.Version(1).IsValid() ? specHitDistRing.Version(1) : specHitDist;
        const RDGTexture fallbackPrevNR = prevNRRing.Version(1).IsValid() ? prevNRRing.Version(1) : prevNR;
        const RDGTexture fallbackViewZ = viewZRing.Version(1).IsValid() ? viewZRing.Version(1) : viewZ;
        const bool hasHitDeltaHistory = hitDelta.IsValid() && reflection.hitDeltaHistory.IsValid();
        const RDGTexture hitDeltaHistory = hasHitDeltaHistory ? reflection.hitDeltaHistory : RDGTexture{};

        pass.Execute([pipelineManager, constants, tiles, guide, specIn, diffIn, width, height, fallbackSpec, fallbackDiff, fallbackSpecFast, fallbackDiffFast, fallbackHistLen, fallbackSpecHitD, fallbackPrevNR, fallbackViewZ,
                hasHitDeltaHistory, hitDeltaHistory, historyLength, specIllum, diffIllum, specFast, diffFast, specHitDist, specReprojConfidence, prevNR, confidence, hitDelta, virtualMotion,
                gbufferOne, bVirtualMotion, mirrorRoughnessMax = reflectionConfig.mirrorRoughnessMax](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxTemporalAccumulationPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .guideIndex = graph.GetSampledImageViewDescriptorIndex(guide),
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
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(historyLength),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specIllum),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffIllum),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(specFast),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(diffFast),
                .outSpecHitDistIndex = graph.GetStorageImageViewDescriptorIndex(specHitDist),
                .outSpecReprojConfidenceIndex = graph.GetStorageImageViewDescriptorIndex(specReprojConfidence),
                .outPrevNRIndex = graph.GetStorageImageViewDescriptorIndex(prevNR),
                .confidenceIndex = confidence.IsValid() ? graph.GetSampledImageViewDescriptorIndex(confidence) : ~0u,
                .hitDeltaIndex = hitDelta.IsValid() ? graph.GetSampledImageViewDescriptorIndex(hitDelta) : ~0u,
                .hitDeltaHistoryIndex = hasHitDeltaHistory ? graph.GetSampledImageViewDescriptorIndex(hitDeltaHistory) : ~0u,
                .virtualMotionOutIndex = bVirtualMotion ? graph.GetStorageImageViewDescriptorIndex(virtualMotion) : ~0u,
                .mirrorRoughnessMax = mirrorRoughnessMax,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_temporal_accumulation"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 15) / 16, 1);
        });
    }


    const RDGTexture atrousSpec0 = graph.CreateTexture("relax_atrous_spec_0"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture atrousSpec1 = graph.CreateTexture("relax_atrous_spec_1"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture atrousDiff0 = graph.CreateTexture("relax_atrous_diff_0"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture atrousDiff1 = graph.CreateTexture("relax_atrous_diff_1"_sid, colorInfo, {std::nullopt}, true);

    // Pass 4: History Fix. Writes the responsive textures in place at short-history pixels; clamping promotes them into the slow output.
    {
        auto& pass = graph.AddPass("[ReLAX] History Fix"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(guide);
        pass.ReadSampledImage(historyLength);
        pass.ReadSampledImage(specIllum);
        pass.ReadSampledImage(diffIllum);
        pass.ReadWriteImage(specFast);
        pass.ReadWriteImage(diffFast);
        if (confidence.IsValid()) { pass.ReadSampledImage(confidence); }
        pass.Execute([pipelineManager, constants, tiles, guide, historyLength, specIllum, diffIllum, specFast, diffFast, confidence, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxHistoryFixPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .guideIndex = graph.GetSampledImageViewDescriptorIndex(guide),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(historyLength),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(specIllum),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffIllum),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specFast),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffFast),
                .confidenceIndex = confidence.IsValid() ? graph.GetSampledImageViewDescriptorIndex(confidence) : ~0u,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_history_fix"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }


    // Pass 5: History Clamping
    {
        const RDGTexture specNoisy = bPrepass ? specPrepass : specInput;
        const RDGTexture diffNoisy = bPrepass ? diffPrepass : diffInput;

        const RDGTexture clampSpecOut = params.enableAntiFirefly ? atrousSpec0 : specHist;
        const RDGTexture clampDiffOut = params.enableAntiFirefly ? atrousDiff0 : diffHist;

        auto& pass = graph.AddPass("[ReLAX] History Clamping"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(viewZ);
        pass.ReadWriteImage(historyLength);
        pass.ReadSampledImage(specFast);
        pass.ReadSampledImage(diffFast);
        pass.ReadSampledImage(specIllum); // raw TA slow history
        pass.ReadSampledImage(diffIllum);
        if (params.enableAntiFirefly) {
            pass.WriteStorageImage(atrousSpec0);
            pass.WriteStorageImage(atrousDiff0);
        } else {
            pass.WriteStorageImage(specHist);
            pass.WriteStorageImage(diffHist);
        }
        pass.ReadSampledImage(specNoisy); // noisy preblur reference
        pass.ReadSampledImage(diffNoisy);
        pass.WriteStorageImage(specFastHist);
        pass.WriteStorageImage(diffFastHist);
        pass.Execute([pipelineManager, constants, tiles, viewZ, historyLength, specFast, diffFast, specIllum, diffIllum, specFastHist, diffFastHist, specNoisy, diffNoisy, clampSpecOut, clampDiffOut, width,
                height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxHistoryClampingPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(viewZ),
                .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(historyLength),
                .specFastIndex = graph.GetSampledImageViewDescriptorIndex(specFast),
                .diffFastIndex = graph.GetSampledImageViewDescriptorIndex(diffFast),
                .specNoisyIndex = graph.GetSampledImageViewDescriptorIndex(specNoisy),
                .diffNoisyIndex = graph.GetSampledImageViewDescriptorIndex(diffNoisy),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(specIllum),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffIllum),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(clampSpecOut),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(clampDiffOut),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(specFastHist),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(diffFastHist),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(historyLength),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_history_clamping"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 6: Anti-Firefly. Writes relax_*_hist so the suppressed result is what gets carried as history.
    // Separate input/output: the shared-memory preload reads neighboring workgroup borders, so in-place would race.
    if (params.enableAntiFirefly) {
        auto& pass = graph.AddPass("[ReLAX] Anti-Firefly"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(viewZ);
        pass.ReadSampledImage(atrousSpec0);
        pass.ReadSampledImage(atrousDiff0);
        pass.WriteStorageImage(specHist);
        pass.WriteStorageImage(diffHist);

        pass.Execute([pipelineManager, constants, tiles, viewZ, atrousSpec0, atrousDiff0, specHist, diffHist, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            RelaxAntiFireflyPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(viewZ),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(atrousSpec0),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(atrousDiff0),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specHist),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffHist),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("relax_antifirefly"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }


    // Pass 7: A-Trous. Never writes relax_*_hist, so it survives to be carried as next frame's slow history.
    {
        const int32_t iters = glm::max(1, params.atrousIterations);
        const int32_t chromaIters = params.bChromaAtrous ? glm::clamp(params.chromaAtrousIterations, 1, 4) : 0;
        const RDGTexture scratchSpec[2] = {atrousSpec0, atrousSpec1};
        const RDGTexture scratchDiff[2] = {atrousDiff0, atrousDiff1};

        for (int32_t i = 0; i < iters; i++) {
            const bool isLast = (i == iters - 1);
            const RDGTexture specIn = (i == 0) ? specHist : scratchSpec[(i - 1) & 1];
            const RDGTexture diffIn = (i == 0) ? diffHist : scratchDiff[(i - 1) & 1];
            const RDGTexture specOut = isLast ? specInput : scratchSpec[i & 1];

            const RDGTexture diffOut = (isLast && chromaIters == 0) ? diffInput : scratchDiff[i & 1];
            const uint32_t stepSize = 1u << static_cast<uint32_t>(i);

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReLAX] ATrous %d", i);

            auto& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
            pass.ReadBuffer(constants);
            pass.ReadSampledImage(tiles);
            pass.ReadSampledImage(guide);
            pass.ReadSampledImage(historyLength);
            pass.ReadSampledImage(specReprojConfidence);
            pass.ReadSampledImage(specIn);
            pass.ReadSampledImage(diffIn);
            pass.WriteStorageImage(specOut);
            pass.WriteStorageImage(diffOut);

            pass.Execute([pipelineManager, constants, tiles, guide, historyLength, specReprojConfidence,
                    specIn, diffIn, specOut, diffOut, stepSize, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    RelaxAtrousPushConstant pc{
                        .constants = graph.GetBufferAddress(constants),
                        .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                        .guideIndex = graph.GetSampledImageViewDescriptorIndex(guide),
                        .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(historyLength),
                        .specVarIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                        .diffVarIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                        .specReprojConfidenceIndex = graph.GetSampledImageViewDescriptorIndex(specReprojConfidence),
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
            const RDGTexture diffIn = scratchDiff[(iters - 1 + c) & 1];
            const RDGTexture diffOut = isLastChroma ? diffInput : scratchDiff[(iters + c) & 1];
            const uint32_t stepSize = chromaStrides[c];

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReLAX] ATrous Chroma %d", c);

            auto& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
            pass.ReadBuffer(constants);
            pass.ReadSampledImage(tiles);
            pass.ReadSampledImage(guide);
            pass.ReadSampledImage(historyLength);
            pass.ReadSampledImage(diffIn);
            pass.WriteStorageImage(diffOut);

            pass.Execute([pipelineManager, constants, tiles, guide, historyLength, diffIn, diffOut, stepSize, width, height, chromaLumaPower = params.chromaLumaPower](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                RelaxAtrousPushConstant pc{
                    .constants = graph.GetBufferAddress(constants),
                    .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                    .guideIndex = graph.GetSampledImageViewDescriptorIndex(guide),
                    .historyLengthIndex = graph.GetSampledImageViewDescriptorIndex(historyLength),
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
        const RDGTexture gbufferTwo = targets.gbufferTwo;
        const bool bDDGI = bDDGIApply && ddgi.cascades.IsValid();
        const RDGBuffer ddgiCascades = ddgi.cascades;
        const bool bGIGather = giGatherMode != 0u && finalGather.resolved.IsValid();
        const RDGTexture giResolved = finalGather.resolved;
        const RDGTexture giData = finalGather.data;
        const RDGTexture giSkyVis = finalGather.skyVisHistory;
        const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
        const RDGTexture reflectionTarget = reflection.specNoisy;
        const bool bReflectionMerged = reflectionConfig.bMergedDenoise && reflectionRoughnessMax >= 0.0f && reflection.specNoisy.IsValid();
        const bool bReflection = !bReflectionMerged && reflectionRoughnessMax >= 0.0f && reflectionTarget.IsValid();
        const RDGBuffer probeGrid = worldGrid.probeGrid;

        const RDGTexture shadows = targets.shadows;

        const bool bScreenDiffuse = targets.restirDiffuseRatio.IsValid() && targets.giScreenDiffuse.IsValid();
        const RDGTexture diffuseRatio = targets.restirDiffuseRatio;
        const RDGTexture screenDiffuse = targets.giScreenDiffuse;
        auto& pass = graph.AddPass("[ReLAX] Remodulate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReLAX);
        if (bScreenDiffuse) {
            pass.ReadSampledImage(diffuseRatio);
            pass.WriteStorageImage(screenDiffuse);
        }
        pass.ReadBuffer(scene.sceneData);
        pass.ReadBuffer(scene.lightData);
        pass.ReadBuffer(scene.reflectionProbes);
        if (probeGrid.IsValid()) { pass.ReadBuffer(probeGrid); }
        pass.ReadSampledImage(diffInput);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(gbufferTwo);
        pass.ReadSampledImage(depth);
        if (shadows.IsValid()) {
            pass.ReadSampledImage(shadows);
        }
        if (bDDGI) {
            AddDDGISampleDependencies(graph, pass, ddgi);
        }
        if (bReflection) {
            pass.ReadSampledImage(reflectionTarget);
        }
        if (bGIGather) {
            pass.ReadSampledImage(giResolved);
            pass.ReadSampledImage(giData);
            pass.ReadSampledImage(giSkyVis);
        }
        pass.WriteStorageImage(noisyInput);

        const int32_t skyboxIndex = viewFamily.skyboxIndex;
        const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
        const bool bProbeBrute = viewFamily.bReflectionProbeBruteForce;
        pass.Execute([pipelineManager, &scene, diffInput, specInput, gbufferOne, gbufferTwo, depth, noisyInput, width, height, remodulateOutputMode, skyboxIndex, iblIntensity, indirectIntensity = viewFamily.indirectIntensity, bDDGI,
                ddgiCascades, shadows, bReflection, bReflectionMerged, reflectionRoughnessMax, reflectionTarget, bGIGather, giResolved, giData, giSkyVis, giGatherMode, reflectionProbeCount, bProbeBrute, probeGrid,
                bScreenDiffuse, diffuseRatio, screenDiffuse](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
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
                .ddgiCascades = bDDGI ? graph.GetBufferAddress(ddgiCascades) : 0,
                .bDDGIApply = bDDGI ? 1u : 0u,
                .shadowsIndex = shadows.IsValid() ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                .reflectionIndex = bReflection ? graph.GetSampledImageViewDescriptorIndex(reflectionTarget) : ~0x0u,
                .reflectionRoughnessMax = reflectionRoughnessMax,
                .giResolvedIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giResolved) : ~0x0u,
                .giDataIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giData) : ~0x0u,
                .giGatherMode = bGIGather ? giGatherMode : 0u,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
                .worldGridProbeGrid = (!bProbeBrute && probeGrid.IsValid()) ? graph.GetBufferAddress(probeGrid) : 0,
                .bReflectionMerged = bReflectionMerged ? 1u : 0u,
                .diffuseRatioIndex = bScreenDiffuse ? graph.GetSampledImageViewDescriptorIndex(diffuseRatio) : ~0x0u,
                .screenDiffuseOutIndex = bScreenDiffuse ? graph.GetStorageImageViewDescriptorIndex(screenDiffuse) : ~0x0u,
                .skyVisIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giSkyVis) : ~0x0u,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("restir_remodulate"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    return virtualMotion;
}

void SetupReBLURDenoiser(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Extent2D renderExtent,
                         const RenderTargets& targets,
                         const SceneResources& scene,
                         const ReSTIRFrame& restir,
                         const ReflectionFrame& reflection,
                         const FinalGatherFrame& finalGather,
                         const DDGIFrame& ddgi,
                         const WorldGridFrame& worldGrid,
                         const Core::ReBLURParams& params,
                         uint64_t frameNumber,
                         uint32_t remodulateOutputMode,
                         float iblIntensity,
                         uint32_t activeCheckerboardField,
                         float checkerboardResolveAccumSpeed,
                         bool bDDGIApply,
                         const Core::ReflectionConfiguration& reflectionConfig,
                          uint32_t giGatherMode,
                          float historyExposureRatio)
{
    ZoneScoped;
    const bool bCheckerboard = activeCheckerboardField != 0u;
    // NRD resolves checkerboard inside the prepass; radius 0 when the user disabled it.
    const bool bPrepass = params.enablePrepass || bCheckerboard;
    const uint32_t width = renderExtent.width;
    const uint32_t height = renderExtent.height;
    const uint32_t tilesW = (width + 15) / 16;
    const uint32_t tilesH = (height + 15) / 16;

    const RDGTexture gbufferOne = targets.gbufferOne;
    const RDGTexture depth = targets.depthCopy;
    const RDGTexture specInput = targets.intermediateTwo;
    const RDGTexture diffInput = targets.intermediateOne;
    const RDGTexture noisyInput = targets.colorOutput;
    const RDGTexture confidence = restir.confidence;

    const TextureInfo colorInfo{VK_FORMAT_R16G16B16A16_SFLOAT, width, height, 1};
    const TextureInfo histLenInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo hitDistInfo{VK_FORMAT_R16_SFLOAT, width, height, 1};
    const TextureInfo tilesInfo{VK_FORMAT_R8_UNORM, tilesW, tilesH, 1};
    const TextureInfo viewZInfo{VK_FORMAT_R32_SFLOAT, width, height, 1};
    const TextureInfo data1Info{VK_FORMAT_R8G8_UNORM, width, height, 1};
    const TextureInfo data2Info{VK_FORMAT_R32_UINT, width, height, 1};

    // History rings must be declared before anything queries them below.
    const RDGTextureRing viewZRing = graph.CreateVersionedTexture("reblur_viewz"_sid, viewZInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing specHistRing = graph.CreateVersionedTexture("reblur_spec_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing diffHistRing = graph.CreateVersionedTexture("reblur_diff_hist"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing specFastFixedRing = graph.CreateVersionedTexture("reblur_spec_fast_fixed"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing diffFastFixedRing = graph.CreateVersionedTexture("reblur_diff_fast_fixed"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing internalDataRing = graph.CreateVersionedTexture("reblur_internal_data"_sid, data2Info, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing specHitDistRing = graph.CreateVersionedTexture("reblur_spec_hit_dist"_sid, hitDistInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing prevNRRing = graph.CreateVersionedTexture("reblur_prev_nr"_sid, colorInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing specLumaStabRing = graph.CreateVersionedTexture("reblur_spec_luma_stab"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing diffLumaStabRing = graph.CreateVersionedTexture("reblur_diff_luma_stab"_sid, histLenInfo, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);

    const RDGTexture viewZ = viewZRing.Current();
    const RDGTexture specHist = specHistRing.Current();
    const RDGTexture diffHist = diffHistRing.Current();
    const RDGTexture specFastFixed = specFastFixedRing.Current();
    const RDGTexture diffFastFixed = diffFastFixedRing.Current();
    const RDGTexture internalData = internalDataRing.Current();
    const RDGTexture specHitDist = specHitDistRing.Current();
    const RDGTexture prevNR = prevNRRing.Current();
    const RDGTexture specLumaStab = specLumaStabRing.Current();
    const RDGTexture diffLumaStab = diffLumaStabRing.Current();

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

    const bool bFirstFrame = !specHistRing.Version(1).IsValid();

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

    {
        constexpr float REBLUR_POST_BLUR_ROTATOR_OFFSET_DEGREES = 22.5f;
        const uint32_t frameIndex = static_cast<uint32_t>(frameNumber);
        const float weylStep = static_cast<float>(frameIndex * 10368889u) / 16777216.0f;
        const float anglePre = glm::fract(1.0f / glm::sqrt(2.0f) + weylStep) * glm::radians(90.0f);
        const float angle = glm::fract(1.0f / glm::sqrt(3.0f) + weylStep) * glm::radians(90.0f);
        rc.gRotatorPre = glm::vec4(glm::cos(anglePre), glm::sin(anglePre), -glm::sin(anglePre), glm::cos(anglePre));
        const float anglePost = angle + glm::radians(REBLUR_POST_BLUR_ROTATOR_OFFSET_DEGREES);
        rc.gRotator = glm::vec4(glm::cos(angle), glm::sin(angle), -glm::sin(angle), glm::cos(angle));
        rc.gRotatorPost = glm::vec4(glm::cos(anglePost), glm::sin(anglePost), -glm::sin(anglePost), glm::cos(anglePost));
    }
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

    rc.gMaxAccumulatedFrameNum = glm::min(params.maxAccumulatedFrameNum * params.framerateScale, REBLUR_MAX_ACCUM_FRAME_NUM);
    rc.gMaxFastAccumulatedFrameNum = glm::min(params.maxFastAccumulatedFrameNum * params.framerateScale, REBLUR_MAX_ACCUM_FRAME_NUM);
    // Zero-init would give responsiveFactor ~1 (no-op); these defaults invert that into max responsiveness.
    rc.gResponsiveAccumulationInvRoughnessThreshold = 1000.0f;
    rc.gResponsiveAccumulationMinAccumulatedFrameNum = 3u;
    rc.gMaxStabilizedFrameNum = params.enableTemporalStabilization ? glm::min(params.maxStabilizedFrameNum * params.framerateScale, REBLUR_MAX_ACCUM_FRAME_NUM) : 0.0f;
    rc.gDisocclusionThreshold = params.disocclusionThreshold;
    rc.gDisocclusionThresholdAlternate = params.disocclusionThresholdAlternate;
    rc.gDenoisingRange = params.denoisingRange;
    rc.gPlaneDistSensitivity = params.planeDistanceSensitivity;
    rc.gFramerateScale = params.framerateScale;
    rc.gHistoryExposureRatio = historyExposureRatio;
    rc.gMinBlurRadius = params.minBlurRadius;
    rc.gMaxBlurRadius = params.maxBlurRadius;
    // Checkerboard forces the prepass to run for hole resolve; "prepass disabled" is radius 0.
    rc.gDiffPrepassBlurRadius = params.enablePrepass ? params.diffusePrepassBlurRadius : 0.0f;
    rc.gSpecPrepassBlurRadius = params.enablePrepass ? params.specularPrepassBlurRadius : 0.0f;
    rc.gLobeAngleFraction = params.lobeAngleFraction * params.lobeAngleFraction;
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
    const HostBufferMapping constantsMapping = graph.OpenHostBuffer("reblur_constants"_sid, sizeof(ReblurDiffuseSpecularConstants));
    memcpy(constantsMapping.data, &rc, sizeof(ReblurDiffuseSpecularConstants));
    const RDGBuffer constants = constantsMapping.buffer;

    // Pass 0: Generate viewZ
    {
        auto& pass = graph.AddPass("[ReBLUR] Generate ViewZ"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.WriteStorageImage(viewZ);
        pass.Execute([pipelineManager, constants, depth, gbufferOne, viewZ, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurGenerateViewZPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outViewZIndex = graph.GetStorageImageViewDescriptorIndex(viewZ),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_generate_viewz"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 1: Classify tiles
    const RDGTexture tiles = graph.CreateTexture("reblur_tiles"_sid, tilesInfo, {std::nullopt}, true);
    {
        auto& pass = graph.AddPass("[ReBLUR] Classify Tiles"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(depth);
        pass.WriteStorageImage(tiles);
        pass.Execute([pipelineManager, constants, depth, tiles, tilesW, tilesH](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurClassifyTilesPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .tilesOutIndex = graph.GetStorageImageViewDescriptorIndex(tiles),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_classify_tiles"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesW, tilesH, 1);
        });
    }

    // Pass 2: Front-end pack (raw RGB + hitDist -> YCoCg + hitDist)
    const RDGTexture specPacked = graph.CreateTexture("reblur_spec_packed"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture diffPacked = graph.CreateTexture("reblur_diff_packed"_sid, colorInfo, {std::nullopt}, true);
    {
        auto& pass = graph.AddPass("[ReBLUR] Pack"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(diffInput);
        pass.WriteStorageImage(specPacked);
        pass.WriteStorageImage(diffPacked);
        pass.Execute([pipelineManager, constants, depth, gbufferOne, specInput, diffInput, specPacked, diffPacked, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurPackPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specInput),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffInput),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex(specPacked),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex(diffPacked),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_pack"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Pass 3: Prepass (optional; forced on for checkerboard hole resolve)
    RDGTexture specPrepass{};
    RDGTexture diffPrepass{};
    RDGTexture specHitDistTracking{};
    if (bPrepass) {
        specPrepass = graph.CreateTexture("reblur_spec_prepass"_sid, colorInfo, {std::nullopt}, true);
        diffPrepass = graph.CreateTexture("reblur_diff_prepass"_sid, colorInfo, {std::nullopt}, true);
        specHitDistTracking = graph.CreateTexture("reblur_spec_hit_dist_tracking"_sid, hitDistInfo, {std::nullopt}, true);

        auto& pass = graph.AddPass("[ReBLUR] Prepass"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(specPacked);
        pass.ReadSampledImage(diffPacked);
        pass.WriteStorageImage(specPrepass);
        pass.WriteStorageImage(diffPrepass);
        pass.WriteStorageImage(specHitDistTracking);
        pass.Execute([pipelineManager, constants, tiles, depth, gbufferOne, specPacked, diffPacked, specPrepass, diffPrepass, specHitDistTracking, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurPrepassPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specPacked),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffPacked),
                .specOutIndex = graph.GetStorageImageViewDescriptorIndex(specPrepass),
                .diffOutIndex = graph.GetStorageImageViewDescriptorIndex(diffPrepass),
                .specHitDistTrackingOutIndex = graph.GetStorageImageViewDescriptorIndex(specHitDistTracking),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_prepass"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
        });
    }

    const RDGTexture specIn = bPrepass ? specPrepass : specPacked;
    const RDGTexture diffIn = bPrepass ? diffPrepass : diffPacked;
    const RDGTexture specHitDistTrackingIn = bPrepass ? specHitDistTracking : specIn;

    const RDGTexture specAccum = graph.CreateTexture("reblur_spec_accum"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture diffAccum = graph.CreateTexture("reblur_diff_accum"_sid, colorInfo, {std::nullopt}, true);
    // Fast (responsive) history is NRD-faithful single-channel luma (R16F), not RGBA.
    const RDGTexture specFast = graph.CreateTexture("reblur_spec_fast"_sid, histLenInfo, {std::nullopt}, true);
    const RDGTexture diffFast = graph.CreateTexture("reblur_diff_fast"_sid, histLenInfo, {std::nullopt}, true);
    // DATA1 = per-lobe accum frames (RG8), DATA2 = occlusion bits + curvature + vha (R32U).
    const RDGTexture data1 = graph.CreateTexture("reblur_data1"_sid, data1Info, {std::nullopt}, true);
    const RDGTexture data2 = graph.CreateTexture("reblur_data2"_sid, data2Info, {std::nullopt}, true);
    const RDGTexture specHfix = graph.CreateTexture("reblur_spec_hfix"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture diffHfix = graph.CreateTexture("reblur_diff_hfix"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture specBlur = graph.CreateTexture("reblur_spec_blur"_sid, colorInfo, {std::nullopt}, true);
    const RDGTexture diffBlur = graph.CreateTexture("reblur_diff_blur"_sid, colorInfo, {std::nullopt}, true);

    // Pass 4: Temporal accumulation
    {
        auto& pass = graph.AddPass("[ReBLUR] Temporal Accumulation"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(specIn);
        pass.ReadSampledImage(diffIn);
        if (bPrepass) { pass.ReadSampledImage(specHitDistTrackingIn); }
        if (specHistRing.Version(1).IsValid()) { pass.ReadSampledImage(specHistRing.Version(1)); }
        if (diffHistRing.Version(1).IsValid()) { pass.ReadSampledImage(diffHistRing.Version(1)); }
        if (specFastFixedRing.Version(1).IsValid()) { pass.ReadSampledImage(specFastFixedRing.Version(1)); }
        if (diffFastFixedRing.Version(1).IsValid()) { pass.ReadSampledImage(diffFastFixedRing.Version(1)); }
        if (internalDataRing.Version(1).IsValid()) { pass.ReadSampledImage(internalDataRing.Version(1)); }
        if (specHitDistRing.Version(1).IsValid()) { pass.ReadSampledImage(specHitDistRing.Version(1)); }
        if (prevNRRing.Version(1).IsValid()) { pass.ReadSampledImage(prevNRRing.Version(1)); }
        if (viewZRing.Version(1).IsValid()) { pass.ReadSampledImage(viewZRing.Version(1)); }
        else { pass.ReadSampledImage(viewZ); }
        if (confidence.IsValid()) { pass.ReadSampledImage(confidence); }
        pass.WriteStorageImage(specAccum);
        pass.WriteStorageImage(diffAccum);
        pass.WriteStorageImage(specFast);
        pass.WriteStorageImage(diffFast);
        pass.WriteStorageImage(data1);
        pass.WriteStorageImage(data2);
        pass.WriteStorageImage(specHitDist);
        pass.WriteStorageImage(prevNR);

        const bool hasHistory = specHistRing.Version(1).IsValid();
        const RDGTexture fallbackSpec = hasHistory ? specHistRing.Version(1) : specIn;
        const RDGTexture fallbackDiff = hasHistory ? diffHistRing.Version(1) : diffIn;
        // First-frame fallbacks must be format-compatible sources (values unused under gResetHistory).
        const RDGTexture fallbackSpecFast = specFastFixedRing.Version(1).IsValid() ? specFastFixedRing.Version(1) : specHitDist;
        const RDGTexture fallbackDiffFast = diffFastFixedRing.Version(1).IsValid() ? diffFastFixedRing.Version(1) : specHitDist;
        const RDGTexture fallbackInternalData = internalDataRing.Version(1).IsValid() ? internalDataRing.Version(1) : data2;
        const RDGTexture fallbackSpecHitD = specHitDistRing.Version(1).IsValid() ? specHitDistRing.Version(1) : specHitDist;
        const RDGTexture fallbackPrevNR = prevNRRing.Version(1).IsValid() ? prevNRRing.Version(1) : prevNR;
        const RDGTexture fallbackViewZ = viewZRing.Version(1).IsValid() ? viewZRing.Version(1) : viewZ;

        pass.Execute([pipelineManager, constants, tiles, gbufferOne, depth, specIn, specHitDistTrackingIn, diffIn, width, height, fallbackSpec, fallbackDiff, fallbackSpecFast, fallbackDiffFast, fallbackInternalData, fallbackSpecHitD,
                fallbackPrevNR, fallbackViewZ, data1, data2, specAccum, diffAccum, specFast, diffFast, specHitDist, prevNR, confidence](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurTemporalAccumulationPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .prevNormalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(fallbackPrevNR),
                .prevViewZIndex = graph.GetSampledImageViewDescriptorIndex(fallbackViewZ),
                .prevInternalDataIndex = graph.GetSampledImageViewDescriptorIndex(fallbackInternalData),
                .specInputIndex = graph.GetSampledImageViewDescriptorIndex(specIn),
                .specHitDistTrackingIndex = graph.GetSampledImageViewDescriptorIndex(specHitDistTrackingIn),
                .diffInputIndex = graph.GetSampledImageViewDescriptorIndex(diffIn),
                .historySpecFastIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecFast),
                .historyDiffFastIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiffFast),
                .historySpecIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpec),
                .historyDiffIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiff),
                .prevSpecHitDistIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecHitD),
                .outData1Index = graph.GetStorageImageViewDescriptorIndex(data1),
                .outData2Index = graph.GetStorageImageViewDescriptorIndex(data2),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specAccum),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffAccum),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(specFast),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(diffFast),
                .outSpecHitDistIndex = graph.GetStorageImageViewDescriptorIndex(specHitDist),
                .outPrevNRIndex = graph.GetStorageImageViewDescriptorIndex(prevNR),
                .confidenceIndex = confidence.IsValid() ? graph.GetSampledImageViewDescriptorIndex(confidence) : ~0u,
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
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(data1);
        pass.ReadSampledImage(specAccum);
        pass.ReadSampledImage(diffAccum);
        pass.ReadSampledImage(specFast);
        pass.ReadSampledImage(diffFast);
        pass.ReadSampledImage(specHitDist);
        pass.WriteStorageImage(specHfix);
        pass.WriteStorageImage(diffHfix);
        pass.WriteStorageImage(specFastFixed);
        pass.WriteStorageImage(diffFastFixed);
        pass.Execute([pipelineManager, constants, tiles, gbufferOne, depth, data1, specAccum, diffAccum, specFast, diffFast, specHitDist, specHfix, diffHfix, specFastFixed, diffFastFixed, width,
                height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurHistoryFixPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .data1Index = graph.GetSampledImageViewDescriptorIndex(data1),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(specAccum),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffAccum),
                .specFastIndex = graph.GetSampledImageViewDescriptorIndex(specFast),
                .diffFastIndex = graph.GetSampledImageViewDescriptorIndex(diffFast),
                .specHitDistIndex = graph.GetSampledImageViewDescriptorIndex(specHitDist),
                .outSpecIndex = graph.GetStorageImageViewDescriptorIndex(specHfix),
                .outDiffIndex = graph.GetStorageImageViewDescriptorIndex(diffHfix),
                .outSpecFastIndex = graph.GetStorageImageViewDescriptorIndex(specFastFixed),
                .outDiffFastIndex = graph.GetStorageImageViewDescriptorIndex(diffFastFixed),
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("reblur_history_fix"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    // Passes 6 & 7: Blur and Post-Blur (one pipeline, isPostBlur toggles radius). Post-blur output is the carried slow history.
    auto addBlur = [&](StringID passName, RDGTexture specSrc, RDGTexture diffSrc, RDGTexture specDst, RDGTexture diffDst, uint32_t isPostBlur) {
        auto& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(data1);
        pass.ReadSampledImage(specSrc);
        pass.ReadSampledImage(diffSrc);
        pass.WriteStorageImage(specDst);
        pass.WriteStorageImage(diffDst);
        pass.Execute([pipelineManager, constants, tiles, gbufferOne, depth, data1, specSrc, diffSrc, specDst, diffDst, isPostBlur, width, height](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurBlurPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .data1Index = graph.GetSampledImageViewDescriptorIndex(data1),
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
    addBlur("[ReBLUR] Blur"_sid, specHfix, diffHfix, specBlur, diffBlur, 0u);
    addBlur("[ReBLUR] Post-Blur"_sid, specBlur, diffBlur, specHist, diffHist, 1u);

    const int32_t chromaIters = params.bChromaAtrous ? glm::clamp(params.chromaAtrousIterations, 1, 4) : 0;
    const RDGTexture stabilizationDiffOut = chromaIters > 0 ? diffBlur : diffInput;

    // Pass 8: Temporal stabilization
    {
        auto& pass = graph.AddPass("[ReBLUR] Temporal Stabilization"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        pass.ReadBuffer(constants);
        pass.ReadSampledImage(tiles);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(depth);
        pass.ReadSampledImage(data1);
        pass.ReadSampledImage(data2);
        pass.ReadSampledImage(specHitDist);
        pass.ReadSampledImage(specHist);
        pass.ReadSampledImage(diffHist);
        if (specLumaStabRing.Version(1).IsValid()) { pass.ReadSampledImage(specLumaStabRing.Version(1)); }
        if (diffLumaStabRing.Version(1).IsValid()) { pass.ReadSampledImage(diffLumaStabRing.Version(1)); }
        pass.WriteStorageImage(specLumaStab);
        pass.WriteStorageImage(diffLumaStab);
        pass.WriteStorageImage(internalData);
        pass.WriteStorageImage(specInput);
        pass.WriteStorageImage(stabilizationDiffOut);
        const RDGTexture fallbackSpecStab = specLumaStabRing.Version(1).IsValid() ? specLumaStabRing.Version(1) : specHitDist;
        const RDGTexture fallbackDiffStab = diffLumaStabRing.Version(1).IsValid() ? diffLumaStabRing.Version(1) : specHitDist;

        pass.Execute([pipelineManager, constants, tiles, gbufferOne, depth, data1, data2, specHitDist, specHist, diffHist, specLumaStab, diffLumaStab, internalData, specInput, stabilizationDiffOut, width, height,
                fallbackSpecStab, fallbackDiffStab](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReblurStabilizationPushConstant pc{
                .constants = graph.GetBufferAddress(constants),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .specIndex = graph.GetSampledImageViewDescriptorIndex(specHist),
                .diffIndex = graph.GetSampledImageViewDescriptorIndex(diffHist),
                .data1Index = graph.GetSampledImageViewDescriptorIndex(data1),
                .data2Index = graph.GetSampledImageViewDescriptorIndex(data2),
                .specHitDistIndex = graph.GetSampledImageViewDescriptorIndex(specHitDist),
                .prevSpecLumaStabIndex = graph.GetSampledImageViewDescriptorIndex(fallbackSpecStab),
                .prevDiffLumaStabIndex = graph.GetSampledImageViewDescriptorIndex(fallbackDiffStab),
                .outSpecLumaStabIndex = graph.GetStorageImageViewDescriptorIndex(specLumaStab),
                .outDiffLumaStabIndex = graph.GetStorageImageViewDescriptorIndex(diffLumaStab),
                .outSpecFinalIndex = graph.GetStorageImageViewDescriptorIndex(specInput),
                .outDiffFinalIndex = graph.GetStorageImageViewDescriptorIndex(stabilizationDiffOut),
                .outInternalDataIndex = graph.GetStorageImageViewDescriptorIndex(internalData),
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
            const RDGTexture inTex = (c & 1) ? diffHfix : diffBlur;
            const RDGTexture outTex = isLastChroma ? diffInput : ((c & 1) ? diffBlur : diffHfix);
            const uint32_t stepSize = chromaStrides[c];

            const Core::InlineString<32> passName = Core::InlineString<32>::Format("[ReBLUR] Chroma %d", c);

            auto& pass = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
            pass.ReadBuffer(constants);
            pass.ReadSampledImage(tiles);
            pass.ReadSampledImage(gbufferOne);
            pass.ReadSampledImage(depth);
            pass.ReadSampledImage(data1);
            pass.ReadSampledImage(inTex);
            pass.WriteStorageImage(outTex);

            pass.Execute([pipelineManager, constants, tiles, gbufferOne, depth, data1, inTex, outTex, stepSize, width, height, chromaLumaPower = params.chromaLumaPower](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                ReblurChromaPushConstant pc{
                    .constants = graph.GetBufferAddress(constants),
                    .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                    .normalRoughnessIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                    .viewZIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                    .data1Index = graph.GetSampledImageViewDescriptorIndex(data1),
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
        const RDGTexture gbufferTwo = targets.gbufferTwo;
        const bool bDDGI = bDDGIApply && ddgi.cascades.IsValid();
        const RDGBuffer ddgiCascades = ddgi.cascades;
        const bool bGIGather = giGatherMode != 0u && finalGather.resolved.IsValid();
        const RDGTexture giResolved = finalGather.resolved;
        const RDGTexture giData = finalGather.data;
        const RDGTexture giSkyVis = finalGather.skyVisHistory;
        const float reflectionRoughnessMax = ComputeReflectionRoughnessMax(reflectionConfig);
        const RDGTexture reflectionTarget = reflection.specNoisy;
        const bool bReflectionMerged = reflectionConfig.bMergedDenoise && reflectionRoughnessMax >= 0.0f && reflection.specNoisy.IsValid();
        const bool bReflection = !bReflectionMerged && reflectionRoughnessMax >= 0.0f && reflectionTarget.IsValid();
        const RDGBuffer probeGrid = worldGrid.probeGrid;

        const RDGTexture shadows = targets.shadows;

        const bool bScreenDiffuse = targets.restirDiffuseRatio.IsValid() && targets.giScreenDiffuse.IsValid();
        const RDGTexture diffuseRatio = targets.restirDiffuseRatio;
        const RDGTexture screenDiffuse = targets.giScreenDiffuse;
        auto& pass = graph.AddPass("[ReBLUR] Remodulate"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ReBLUR);
        if (bScreenDiffuse) {
            pass.ReadSampledImage(diffuseRatio);
            pass.WriteStorageImage(screenDiffuse);
        }
        pass.ReadBuffer(scene.sceneData);
        pass.ReadBuffer(scene.lightData);
        pass.ReadBuffer(scene.reflectionProbes);
        if (probeGrid.IsValid()) { pass.ReadBuffer(probeGrid); }
        pass.ReadSampledImage(diffInput);
        pass.ReadSampledImage(specInput);
        pass.ReadSampledImage(gbufferOne);
        pass.ReadSampledImage(gbufferTwo);
        pass.ReadSampledImage(depth);
        if (shadows.IsValid()) {
            pass.ReadSampledImage(shadows);
        }
        if (bDDGI) {
            AddDDGISampleDependencies(graph, pass, ddgi);
        }
        if (bReflection) {
            pass.ReadSampledImage(reflectionTarget);
        }
        if (bGIGather) {
            pass.ReadSampledImage(giResolved);
            pass.ReadSampledImage(giData);
            pass.ReadSampledImage(giSkyVis);
        }
        pass.WriteStorageImage(noisyInput);

        const int32_t skyboxIndex = viewFamily.skyboxIndex;
        const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
        const bool bProbeBrute = viewFamily.bReflectionProbeBruteForce;
        pass.Execute([pipelineManager, &scene, diffInput, specInput, gbufferOne, gbufferTwo, depth, noisyInput, width, height, remodulateOutputMode, skyboxIndex, iblIntensity, indirectIntensity = viewFamily.indirectIntensity, bDDGI,
                ddgiCascades, shadows, bReflection, bReflectionMerged, reflectionRoughnessMax, reflectionTarget, bGIGather, giResolved, giData, giSkyVis, giGatherMode, reflectionProbeCount, bProbeBrute, probeGrid,
                bScreenDiffuse, diffuseRatio, screenDiffuse](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            ReSTIRRemodulatePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
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
                .ddgiCascades = bDDGI ? graph.GetBufferAddress(ddgiCascades) : 0,
                .bDDGIApply = bDDGI ? 1u : 0u,
                .shadowsIndex = shadows.IsValid() ? graph.GetSampledImageViewDescriptorIndex(shadows) : ~0x0u,
                .reflectionIndex = bReflection ? graph.GetSampledImageViewDescriptorIndex(reflectionTarget) : ~0x0u,
                .reflectionRoughnessMax = reflectionRoughnessMax,
                .giResolvedIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giResolved) : ~0x0u,
                .giDataIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giData) : ~0x0u,
                .giGatherMode = bGIGather ? giGatherMode : 0u,
                .reflectionProbeCount = reflectionProbeCount,
                .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
                .worldGridProbeGrid = (!bProbeBrute && probeGrid.IsValid()) ? graph.GetBufferAddress(probeGrid) : 0,
                .bReflectionMerged = bReflectionMerged ? 1u : 0u,
                .diffuseRatioIndex = bScreenDiffuse ? graph.GetSampledImageViewDescriptorIndex(diffuseRatio) : ~0x0u,
                .screenDiffuseOutIndex = bScreenDiffuse ? graph.GetStorageImageViewDescriptorIndex(screenDiffuse) : ~0x0u,
                .skyVisIndex = bGIGather ? graph.GetSampledImageViewDescriptorIndex(giSkyVis) : ~0x0u,
            };
            const PipelineEntry* p = pipelineManager->GetPipelineEntry("restir_remodulate"_sid);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
            vkCmdPushConstants(cmd, p->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        });
    }
}
} // Render