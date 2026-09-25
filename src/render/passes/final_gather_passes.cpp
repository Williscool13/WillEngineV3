//
// Created by William on 2026-07-12.
//

#include "render/passes/final_gather_passes.h"

#include <tracy/Tracy.hpp>

#include "render/passes/ddgi_passes.h"
#include "render/render_utils.h"
#include "render/interface/render_interface.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
void SetupObjectMotion(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex)
{
    ZoneScoped;
    if (graph.HasTexture(OBJECT_MOTION)) { return; }
    graph.CreateTexture(OBJECT_MOTION, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass("Object Motion Extract"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Untagged);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    pass.WriteStorageImage(OBJECT_MOTION);
    pass.Execute([pipelineManager, sceneIndex, renderExtent, gbufferOne = targets.gbufferOne, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("object_motion_extract"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ObjectMotionExtractPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(OBJECT_MOTION),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + POST_PROCESS_MOTION_BLUR_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_X, (renderExtent[1] + POST_PROCESS_MOTION_BLUR_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_Y, 1);
    });
}

FinalGatherFrame SetupFinalGather(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, uint64_t frameNumber,
    bool bDenoise, bool bTemporalFilter, uint32_t raysPerPixel, bool bDebugView, bool bDisableScreenTier, bool bQuarterRes, float bounceIntensity, float maxRayRadiance)
{
    ZoneScoped;
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(SCENE_DATA_BUFFER) || !graph.HasBuffer(RADIANCE_CACHE_ENTRIES) || !graph.HasBuffer(RADIANCE_CACHE_CELLS)
        || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_PRIMITIVE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER)
        || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER) || !graph.HasBuffer(GEOMETRY_INDEX_BUFFER) || !graph.HasBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER)) {
        return {};
    }

    const uint32_t gatherScale = bQuarterRes ? 4u : 2u;
    const Core::Array<uint32_t, 2> gatherExtent = {(renderExtent[0] + gatherScale - 1u) / gatherScale, (renderExtent[1] + gatherScale - 1u) / gatherScale};
    const StringID gatherShR = bDenoise ? GI_GATHER_RAW_SH_R : GI_GATHER_SH_R;
    const StringID gatherShG = bDenoise ? GI_GATHER_RAW_SH_G : GI_GATHER_SH_G;
    const StringID gatherShB = bDenoise ? GI_GATHER_RAW_SH_B : GI_GATHER_SH_B;
    const StringID gatherSkyVis = bDenoise ? GI_GATHER_RAW_SKY_VIS : GI_GATHER_SKY_VIS;
    graph.CreateTexture(gatherShR, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(gatherShG, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(gatherShB, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(gatherSkyVis, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_DATA, TextureInfo{VK_FORMAT_R16G16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_GUIDE, TextureInfo{VK_FORMAT_R32G32_UINT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateVersionedTexture(GI_GATHER_HISTORY, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const StringID gatherHistory = graph.ResourceVersionID(GI_GATHER_HISTORY, 1);
    graph.CreateTexture(GI_GATHER_RESOLVED, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_GUIDE_NORMAL, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateVersionedTexture(GI_GATHER_FAST, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const StringID fastHistory = graph.ResourceVersionID(GI_GATHER_FAST, 1);
    graph.CreateVersionedTexture(GI_GATHER_SKY_VIS_HISTORY, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const StringID skyVisHistory = graph.ResourceVersionID(GI_GATHER_SKY_VIS_HISTORY, 1);
    graph.CreateVersionedTexture(GI_GATHER_NOISE, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const StringID noiseHistory = graph.ResourceVersionID(GI_GATHER_NOISE, 1);

    const StringID litHistory = graph.ResourceVersionID("lit_color_preoverlay"_sid, 1);
    const StringID depthHistory = graph.ResourceVersionID(targets.depthCopy, 1);
    const StringID gbufferOneHistory = graph.ResourceVersionID(targets.gbufferOne, 1);

    const bool bScreenSpace = !bDebugView && !bDisableScreenTier && graph.ResourceHasVersion("lit_color_preoverlay"_sid, 1) && graph.ResourceHasVersion(targets.depthCopy, 1) && graph.ResourceHasVersion(targets.gbufferOne, 1);
    const bool bScreenDiffuse = bScreenSpace && graph.ResourceHasVersion(GI_SCREEN_DIFFUSE, 1);
    const StringID screenDiffuseHistory = graph.ResourceVersionID(GI_SCREEN_DIFFUSE, 1);

    const uint32_t gatherRayCount = glm::clamp(raysPerPixel, 1u, GI_GATHER_MAX_RAYS_PER_PIXEL);
    graph.CreateBuffer("gi_gather_hits"_sid, static_cast<VkDeviceSize>(gatherExtent[0]) * gatherExtent[1] * gatherRayCount * sizeof(GIGatherHit), true);

    RenderPass& tracePass = graph.AddPass("GI Diffuse Gather Trace"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    tracePass.ReadTLASBuffer(RT_TLAS_BUFFER);
    tracePass.ReadBuffer(SCENE_DATA_BUFFER);
    tracePass.ReadSampledImage(targets.gbufferOne);
    tracePass.ReadSampledImage(targets.depthCopy);
    tracePass.WriteBuffer("gi_gather_hits"_sid);
    tracePass.Execute([pipelineManager, sceneIndex, frameNumber, gatherExtent, renderExtent, gatherScale, gatherRayCount,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_gather_trace"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIGatherPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .gatherExtent = {gatherExtent[0], gatherExtent[1]},
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .rayCount = gatherRayCount,
            .gatherScale = gatherScale,
            .hitBuffer = graph.GetBufferAddress("gi_gather_hits"_sid),
            .screenDiffuseHistoryIndex = ~0x0u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (gatherExtent[0] + 7) / 8, (gatherExtent[1] + 7) / 8, 1);
    });

    RenderPass& pass = graph.AddPass("GI Diffuse Gather Shade"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(RADIANCE_CACHE_ENTRIES);
    pass.ReadBuffer(RADIANCE_CACHE_KEYS);
    pass.ReadBuffer(RADIANCE_CACHE_CELLS);
    const StringID touchEntries = RADIANCE_CACHE_TOUCH_ENTRIES;
    const bool bTouch = graph.HasBuffer(touchEntries);
    if (bTouch) {
        pass.ReadWriteBuffer(touchEntries);
    }
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (graph.HasBuffer("world_grid_probe_grid"_sid)) { pass.ReadBuffer("world_grid_probe_grid"_sid); }
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    if (bScreenSpace) {
        pass.ReadSampledImage(litHistory);
        pass.ReadSampledImage(depthHistory);
        pass.ReadSampledImage(gbufferOneHistory);
    }
    if (bScreenDiffuse) {
        pass.ReadSampledImage(screenDiffuseHistory);
    }
    const bool bCascades = AddDDGISampleDependencies(graph, pass);
    pass.ReadBuffer("gi_gather_hits"_sid);
    pass.WriteStorageImage(gatherShR);
    pass.WriteStorageImage(gatherShG);
    pass.WriteStorageImage(gatherShB);
    pass.WriteStorageImage(gatherSkyVis);
    pass.WriteStorageImage(GI_GATHER_DATA);
    pass.WriteStorageImage(GI_GATHER_GUIDE);

    const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
    const bool bProbeBrute = viewFamily.bReflectionProbeBruteForce;
    pass.Execute([pipelineManager, sceneIndex, frameNumber, gatherExtent, renderExtent, gatherScale, gatherRayCount, bCascades, bScreenSpace, gatherShR, gatherShG, gatherShB, gatherSkyVis, reflectionProbeCount, bProbeBrute, bTouch, touchEntries, litHistory, depthHistory, gbufferOneHistory, bScreenDiffuse, screenDiffuseHistory,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy,
            skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity, bounceIntensity = glm::clamp(bounceIntensity, 0.0f, 1.0f), maxRayRadiance = glm::max(maxRayRadiance, 0.0f)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_gather_shade"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIGatherPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .ddgiCascades = bCascades ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
            .cacheEntries = graph.GetBufferAddress(RADIANCE_CACHE_ENTRIES),
            .cacheKeys = graph.GetBufferAddress(RADIANCE_CACHE_KEYS),
            .cacheCells = graph.GetBufferAddress(RADIANCE_CACHE_CELLS),
            .instanceBuffer = graph.GetBufferAddress(GEOMETRY_INSTANCE_BUFFER),
            .primitiveBuffer = graph.GetBufferAddress(GEOMETRY_PRIMITIVE_BUFFER),
            .modelBuffer = graph.GetBufferAddress(GEOMETRY_MODEL_BUFFER),
            .materialBuffer = graph.GetBufferAddress(GEOMETRY_MATERIAL_BUFFER),
            .indexBuffer = graph.GetBufferAddress(GEOMETRY_INDEX_BUFFER),
            .vertexAttrBuffer = graph.GetBufferAddress(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER),
            .gatherExtent = {gatherExtent[0], gatherExtent[1]},
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .shRIndex = graph.GetStorageImageViewDescriptorIndex(gatherShR),
            .shGIndex = graph.GetStorageImageViewDescriptorIndex(gatherShG),
            .shBIndex = graph.GetStorageImageViewDescriptorIndex(gatherShB),
            .dataIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_DATA),
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .skyboxIndex = skyboxIndex,
            .iblIntensity = iblIntensity,
            .bCascadesValid = bCascades ? 1u : 0u,
            .litHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(litHistory) : ~0x0u,
            .depthHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0x0u,
            .gbufferOneHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0x0u,
            .guideOutIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_GUIDE),
            .reflectionProbeCount = reflectionProbeCount,
            .skyVisIndex = graph.GetStorageImageViewDescriptorIndex(gatherSkyVis),
            .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
            .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer("world_grid_probe_grid"_sid)) ? graph.GetBufferAddress("world_grid_probe_grid"_sid) : 0,
            .rayCount = gatherRayCount,
            .gatherScale = gatherScale,
            .touchEntries = bTouch ? graph.GetBufferAddress(touchEntries) : 0,
            .hitBuffer = graph.GetBufferAddress("gi_gather_hits"_sid),
            .bounceIntensity = bounceIntensity,
            .screenDiffuseHistoryIndex = bScreenDiffuse ? graph.GetSampledImageViewDescriptorIndex(screenDiffuseHistory) : ~0x0u,
            .maxRayRadiance = maxRayRadiance,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (gatherExtent[0] + 7u) / 8u, (gatherExtent[1] + 7u) / 8u, 1);
    });

    const bool bTemporal = bTemporalFilter && graph.ResourceHasVersion(GI_GATHER_HISTORY, 1) && graph.ResourceHasVersion(targets.depthCopy, 1) && graph.ResourceHasVersion(targets.gbufferOne, 1);

    if (bDenoise) {
        graph.CreateTexture(GI_GATHER_TMP_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_TMP_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_TMP_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_TMP_SKY_VIS, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SKY_VIS, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);

        constexpr uint32_t denoiseStrides[] = {1u, 2u, 4u};
        for (uint32_t iteration = 0; iteration < 3u; iteration++) {
            const uint32_t stepSize = denoiseStrides[iteration];
            for (uint32_t direction = 0; direction < 2; direction++) {
                const StringID srcShR = direction != 0 ? GI_GATHER_TMP_SH_R : (iteration == 0 ? GI_GATHER_RAW_SH_R : GI_GATHER_SH_R);
                const StringID srcShG = direction != 0 ? GI_GATHER_TMP_SH_G : (iteration == 0 ? GI_GATHER_RAW_SH_G : GI_GATHER_SH_G);
                const StringID srcShB = direction != 0 ? GI_GATHER_TMP_SH_B : (iteration == 0 ? GI_GATHER_RAW_SH_B : GI_GATHER_SH_B);
                const StringID dstShR = direction == 0 ? GI_GATHER_TMP_SH_R : GI_GATHER_SH_R;
                const StringID dstShG = direction == 0 ? GI_GATHER_TMP_SH_G : GI_GATHER_SH_G;
                const StringID dstShB = direction == 0 ? GI_GATHER_TMP_SH_B : GI_GATHER_SH_B;
                const StringID srcSkyVis = direction != 0 ? GI_GATHER_TMP_SKY_VIS : (iteration == 0 ? GI_GATHER_RAW_SKY_VIS : GI_GATHER_SKY_VIS);
                const StringID dstSkyVis = direction == 0 ? GI_GATHER_TMP_SKY_VIS : GI_GATHER_SKY_VIS;

                const Core::InlineString<40> passName = Core::InlineString<40>::Format("GI Diffuse Denoise %s %u", direction == 0 ? "H" : "V", iteration);
                RenderPass& blur = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
                blur.ReadBuffer(SCENE_DATA_BUFFER);
                blur.ReadSampledImage(GI_GATHER_GUIDE);
                blur.ReadSampledImage(GI_GATHER_DATA);
                blur.ReadSampledImage(srcShR);
                blur.ReadSampledImage(srcShG);
                blur.ReadSampledImage(srcShB);
                blur.ReadSampledImage(srcSkyVis);
                blur.WriteStorageImage(dstShR);
                blur.WriteStorageImage(dstShG);
                blur.WriteStorageImage(dstShB);
                blur.WriteStorageImage(dstSkyVis);

                blur.Execute([pipelineManager, sceneIndex, gatherExtent, renderExtent, gatherScale, direction, stepSize, srcShR, srcShG, srcShB, dstShR, dstShG, dstShB, srcSkyVis, dstSkyVis](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_denoise"_sid);
                    if (!pipelineEntry) {
                        return;
                    }
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                    GIDenoisePushConstant pc{
                        .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                        .gatherExtent = {gatherExtent[0], gatherExtent[1]},
                        .renderExtent = {renderExtent[0], renderExtent[1]},
                        .sceneDataIndex = sceneIndex,
                        .guideIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_GUIDE),
                        .dataIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_DATA),
                        .srcShRIndex = graph.GetSampledImageViewDescriptorIndex(srcShR),
                        .srcShGIndex = graph.GetSampledImageViewDescriptorIndex(srcShG),
                        .srcShBIndex = graph.GetSampledImageViewDescriptorIndex(srcShB),
                        .dstShRIndex = graph.GetStorageImageViewDescriptorIndex(dstShR),
                        .dstShGIndex = graph.GetStorageImageViewDescriptorIndex(dstShG),
                        .dstShBIndex = graph.GetStorageImageViewDescriptorIndex(dstShB),
                        .direction = direction,
                        .stepSize = stepSize,
                        .srcSkyVisIndex = graph.GetSampledImageViewDescriptorIndex(srcSkyVis),
                        .dstSkyVisIndex = graph.GetStorageImageViewDescriptorIndex(dstSkyVis),
                        .gatherScale = gatherScale,
                    };
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, (gatherExtent[0] + 15u) / 16u, (gatherExtent[1] + 15u) / 16u, 1);
                });
            }
        }
    }

    RenderPass& upscale = graph.AddPass("GI Diffuse Upscale"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    upscale.ReadBuffer(SCENE_DATA_BUFFER);
    upscale.ReadSampledImage(targets.gbufferOne);
    upscale.ReadSampledImage(targets.depthCopy);
    upscale.ReadSampledImage(GI_GATHER_SH_R);
    upscale.ReadSampledImage(GI_GATHER_SH_G);
    upscale.ReadSampledImage(GI_GATHER_SH_B);
    upscale.ReadSampledImage(GI_GATHER_SKY_VIS);
    upscale.ReadSampledImage(GI_GATHER_DATA);
    upscale.ReadSampledImage(GI_GATHER_GUIDE);
    upscale.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (graph.HasBuffer("world_grid_probe_grid"_sid)) { upscale.ReadBuffer("world_grid_probe_grid"_sid); }
    const bool bFastHistory = bTemporal && graph.ResourceHasVersion(GI_GATHER_FAST, 1);
    const bool bSkyVisHistory = bTemporal && graph.ResourceHasVersion(GI_GATHER_SKY_VIS_HISTORY, 1);
    if (bTemporal) {
        upscale.ReadSampledImage(gatherHistory);
        upscale.ReadSampledImage(depthHistory);
        upscale.ReadSampledImage(gbufferOneHistory);
    }
    if (bFastHistory) {
        upscale.ReadSampledImage(fastHistory);
    }
    if (bSkyVisHistory) {
        upscale.ReadSampledImage(skyVisHistory);
    }
    const bool bNoiseHistory = bTemporal && graph.ResourceHasVersion(GI_GATHER_NOISE, 1);
    if (bNoiseHistory) {
        upscale.ReadSampledImage(noiseHistory);
    }
    const bool bBentNormals = graph.HasTexture("gtao_bent_normals"_sid);
    if (bBentNormals) {
        upscale.ReadSampledImage("gtao_bent_normals"_sid);
    }
    const bool bUpscaleCascades = AddDDGISampleDependencies(graph, upscale);
    upscale.WriteStorageImage(GI_GATHER_HISTORY);
    upscale.WriteStorageImage(GI_GATHER_FAST);
    upscale.WriteStorageImage(GI_GATHER_SKY_VIS_HISTORY);
    upscale.WriteStorageImage(GI_GATHER_NOISE);
    upscale.WriteStorageImage(GI_GATHER_GUIDE_NORMAL);

    upscale.Execute([pipelineManager, sceneIndex, gatherExtent, renderExtent, gatherScale, bTemporal, bFastHistory, bSkyVisHistory, bNoiseHistory, bBentNormals, bUpscaleCascades, reflectionProbeCount, bProbeBrute, gatherHistory, fastHistory, skyVisHistory, noiseHistory, depthHistory, gbufferOneHistory,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy,
            skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_upscale"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIUpscalePushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .gatherExtent = {gatherExtent[0], gatherExtent[1]},
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .shRIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_SH_R),
            .shGIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_SH_G),
            .shBIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_SH_B),
            .historyIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(gatherHistory) : ~0x0u,
            .depthHistoryIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0x0u,
            .gbufferOneHistoryIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0x0u,
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_HISTORY),
            .guideIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_GUIDE),
            .bHistoryValid = bTemporal ? 1u : 0u,
            .dataIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_DATA),
            .skyVisIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_SKY_VIS),
            .ddgiCascades = bUpscaleCascades ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
            .skyboxIndex = skyboxIndex,
            .iblIntensity = iblIntensity,
            .bCascadesValid = bUpscaleCascades ? 1u : 0u,
            .bentNormalIndex = bBentNormals ? graph.GetSampledImageViewDescriptorIndex("gtao_bent_normals"_sid) : ~0x0u,
            .reflectionProbeCount = reflectionProbeCount,
            .gatherScale = gatherScale,
            .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
            .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer("world_grid_probe_grid"_sid)) ? graph.GetBufferAddress("world_grid_probe_grid"_sid) : 0,
            .fastHistoryIndex = bFastHistory ? graph.GetSampledImageViewDescriptorIndex(fastHistory) : ~0x0u,
            .fastOutIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_FAST),
            .skyVisHistoryIndex = bSkyVisHistory ? graph.GetSampledImageViewDescriptorIndex(skyVisHistory) : ~0x0u,
            .skyVisOutIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_SKY_VIS_HISTORY),
            .noiseHistoryIndex = bNoiseHistory ? graph.GetSampledImageViewDescriptorIndex(noiseHistory) : ~0x0u,
            .noiseOutIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_NOISE),
            .guideNormalOutIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_GUIDE_NORMAL),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 15u) / 16u, (renderExtent[1] + 15u) / 16u, 1);
    });

    RenderPass& postBlur = graph.AddPass("GI Diffuse Post Blur"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    postBlur.ReadBuffer(SCENE_DATA_BUFFER);
    postBlur.ReadSampledImage(GI_GATHER_HISTORY);
    postBlur.ReadSampledImage(GI_GATHER_NOISE);
    postBlur.ReadSampledImage(GI_GATHER_GUIDE_NORMAL);
    postBlur.ReadSampledImage(targets.depthCopy);
    postBlur.WriteStorageImage(GI_GATHER_RESOLVED);
    postBlur.Execute([pipelineManager, sceneIndex, renderExtent, gatherScale, frameNumber, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_post_blur"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIPostBlurPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .inputIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_HISTORY),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_RESOLVED),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .guideNormalIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_GUIDE_NORMAL),
            .gatherScale = gatherScale,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .noiseIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_NOISE),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 15u) / 16u, (renderExtent[1] + 15u) / 16u, 1);
    });

    return FinalGatherFrame{.bValid = true};
}

void SetupGIDeconstruct(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, int32_t mode)
{
    ZoneScoped;
    if (mode <= 0 || !graph.HasBuffer(SCENE_DATA_BUFFER) || !graph.HasBuffer(RADIANCE_CACHE_ENTRIES) || !graph.HasBuffer(RADIANCE_CACHE_KEYS) || !graph.HasBuffer(RADIANCE_CACHE_CELLS)) {
        return;
    }

    graph.CreateTexture(GI_DECONSTRUCT_TARGET, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass("GI Deconstruct"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(RADIANCE_CACHE_ENTRIES);
    pass.ReadBuffer(RADIANCE_CACHE_KEYS);
    pass.ReadBuffer(RADIANCE_CACHE_CELLS);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    const bool bCascades = AddDDGISampleDependencies(graph, pass);
    pass.WriteStorageImage(GI_DECONSTRUCT_TARGET);
    pass.Execute([pipelineManager, sceneIndex, renderExtent, bCascades, mode, gbufferOne = targets.gbufferOne, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_deconstruct"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIDeconstructPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .ddgiCascades = bCascades ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
            .cacheEntries = graph.GetBufferAddress(RADIANCE_CACHE_ENTRIES),
            .cacheKeys = graph.GetBufferAddress(RADIANCE_CACHE_KEYS),
            .cacheCells = graph.GetBufferAddress(RADIANCE_CACHE_CELLS),
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .sceneDataIndex = sceneIndex,
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(GI_DECONSTRUCT_TARGET),
            .mode = static_cast<uint32_t>(mode),
            .bCascadesValid = bCascades ? 1u : 0u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 15u) / 16u, (renderExtent[1] + 15u) / 16u, 1);
    });
}

void SetupGIGatherDebug(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, int32_t mode, bool bQuarterRes)
{
    ZoneScoped;
    if (mode <= 0 || !graph.HasTexture(GI_GATHER_RESOLVED) || !graph.HasTexture(GI_GATHER_DATA)) {
        return;
    }

    graph.CreateTexture(GI_GATHER_DEBUG_TARGET, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass("GI Gather Debug"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadSampledImage(GI_GATHER_RESOLVED);
    pass.ReadSampledImage(GI_GATHER_DATA);
    pass.WriteStorageImage(GI_GATHER_DEBUG_TARGET);
    pass.Execute([pipelineManager, renderExtent, mode, bQuarterRes](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_gather_debug"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIGatherDebugPushConstant pc{
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .resolvedIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_RESOLVED),
            .dataIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_DATA),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_DEBUG_TARGET),
            // GIGatherDebugColor keeps the composite-era 2-based numbering.
            .mode = static_cast<uint32_t>(mode) + 1u,
            .gatherScale = bQuarterRes ? 4u : 2u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 15u) / 16u, (renderExtent[1] + 15u) / 16u, 1);
    });
}
} // Render
