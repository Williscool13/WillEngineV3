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

    RenderPass& pass = graph.AddPass(SID("Object Motion Extract"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Untagged);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    pass.WriteStorageImage(OBJECT_MOTION);
    pass.Execute([pipelineManager, sceneIndex, renderExtent, gbufferOne = targets.gbufferOne, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("object_motion_extract"));
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

FinalGatherFrame SetupFinalGather(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, uint64_t frameNumber, bool bDenoise, uint32_t chromaDenoisePasses, float chromaLumaPower, bool bTemporalFilter, bool bSkipRay, uint32_t raysPerPixel, bool bDebugView, bool bDisableScreenTier, bool bQuarterRes, bool bDebugUpscalePath)
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
    graph.CreateTexture(gatherSkyVis, TextureInfo{VK_FORMAT_R8_UNORM, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    // Variance guide: DDGI-predicted relative deviation of a 1spp gather (encoded /GI_GATHER_VARIANCE_GUIDE_SCALE); probe-smooth, so gather-res R8 suffices. Not denoised.
    graph.CreateTexture(GI_GATHER_VARIANCE_GUIDE, TextureInfo{VK_FORMAT_R8_UNORM, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_DATA, TextureInfo{VK_FORMAT_R16G16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_GUIDE, TextureInfo{VK_FORMAT_R32G32_UINT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);

    const bool bScreenSpace = !bDebugView && !bDisableScreenTier && graph.HasTexture(SID("lit_color_history")) && graph.HasTexture(SID("depth_history")) && graph.HasTexture(SID("gbuffer_one_history"));
    const bool bDemodulate = bScreenSpace && graph.HasTexture(GI_GATHER_HISTORY);

    RenderPass& pass = graph.AddPass(SID("GI Diffuse Gather"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(RADIANCE_CACHE_ENTRIES);
    pass.ReadBuffer(RADIANCE_CACHE_KEYS);
    pass.ReadWriteBuffer(RADIANCE_CACHE_CELLS);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (graph.HasBuffer(SID("world_grid_probe_grid"))) { pass.ReadBuffer(SID("world_grid_probe_grid")); }
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    if (bScreenSpace) {
        pass.ReadSampledImage(SID("lit_color_history"));
        pass.ReadSampledImage(SID("depth_history"));
        pass.ReadSampledImage(SID("gbuffer_one_history"));
    }
    if (bDemodulate) {
        pass.ReadSampledImage(GI_GATHER_HISTORY);
    }
    const bool bCascades = AddDDGISampleDependencies(graph, pass);
    pass.WriteStorageImage(gatherShR);
    pass.WriteStorageImage(gatherShG);
    pass.WriteStorageImage(gatherShB);
    pass.WriteStorageImage(gatherSkyVis);
    pass.WriteStorageImage(GI_GATHER_VARIANCE_GUIDE);
    pass.WriteStorageImage(GI_GATHER_DATA);
    pass.WriteStorageImage(GI_GATHER_GUIDE);

    const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
    const bool bProbeBrute = viewFamily.bReflectionProbeBruteForce;
    pass.Execute([pipelineManager, sceneIndex, frameNumber, gatherExtent, renderExtent, gatherScale, bCascades, bScreenSpace, bDemodulate, bSkipRay, raysPerPixel, gatherShR, gatherShG, gatherShB, gatherSkyVis, reflectionProbeCount, bProbeBrute,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy, bakedDiffuseClampK = viewFamily.bakedDiffuseClampK,
            skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gi_gather"));
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
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(RT_TLAS_BUFFER),
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
            .litHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(SID("lit_color_history")) : ~0x0u,
            .depthHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : ~0x0u,
            .gbufferOneHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : ~0x0u,
            .bSkipRay = bSkipRay ? 1u : 0u,
            .guideOutIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_GUIDE),
            .reflectionProbeCount = reflectionProbeCount,
            .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
            .bakedDiffuseClampK = bakedDiffuseClampK,
            .skyVisIndex = graph.GetStorageImageViewDescriptorIndex(gatherSkyVis),
            .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer(SID("world_grid_probe_grid"))) ? graph.GetBufferAddress(SID("world_grid_probe_grid")) : 0,
            .rayCount = glm::clamp(raysPerPixel, 1u, GI_GATHER_MAX_RAYS_PER_PIXEL),
            .giHistoryIndex = bDemodulate ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_HISTORY) : ~0x0u,
            .varGuideOutIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_VARIANCE_GUIDE),
            .gatherScale = gatherScale,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (gatherExtent[0] + 7u) / 8u, (gatherExtent[1] + 7u) / 8u, 1);
    });

    const bool bTemporal = bTemporalFilter && graph.HasTexture(GI_GATHER_HISTORY) && graph.HasTexture(SID("depth_history")) && graph.HasTexture(SID("gbuffer_one_history"));
    const bool bAO = graph.HasTexture(SID("shadows_resolve_target"));

    if (bDenoise) {
        graph.CreateTexture(GI_GATHER_TMP_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_TMP_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_TMP_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_TMP_SKY_VIS, TextureInfo{VK_FORMAT_R8_UNORM, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SKY_VIS, TextureInfo{VK_FORMAT_R8_UNORM, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);

        constexpr uint32_t denoiseStrides[] = {1u, 2u, 4u, 8u, 16u, 32u, 64u};
        const uint32_t denoiseIterations = 3u + glm::min(chromaDenoisePasses, 4u);
        for (uint32_t iteration = 0; iteration < denoiseIterations; iteration++) {
            const uint32_t stepSize = denoiseStrides[iteration];
            const bool bChromaPass = iteration >= 3u;
            for (uint32_t direction = 0; direction < 2; direction++) {
                const StringID srcShR = direction != 0 ? GI_GATHER_TMP_SH_R : (iteration == 0 ? GI_GATHER_RAW_SH_R : GI_GATHER_SH_R);
                const StringID srcShG = direction != 0 ? GI_GATHER_TMP_SH_G : (iteration == 0 ? GI_GATHER_RAW_SH_G : GI_GATHER_SH_G);
                const StringID srcShB = direction != 0 ? GI_GATHER_TMP_SH_B : (iteration == 0 ? GI_GATHER_RAW_SH_B : GI_GATHER_SH_B);
                const StringID dstShR = direction == 0 ? GI_GATHER_TMP_SH_R : GI_GATHER_SH_R;
                const StringID dstShG = direction == 0 ? GI_GATHER_TMP_SH_G : GI_GATHER_SH_G;
                const StringID dstShB = direction == 0 ? GI_GATHER_TMP_SH_B : GI_GATHER_SH_B;
                const StringID srcSkyVis = direction != 0 ? GI_GATHER_TMP_SKY_VIS : (iteration == 0 ? GI_GATHER_RAW_SKY_VIS : GI_GATHER_SKY_VIS);
                const StringID dstSkyVis = direction == 0 ? GI_GATHER_TMP_SKY_VIS : GI_GATHER_SKY_VIS;

                const Core::InlineString<40> passName = Core::InlineString<40>::Format("GI Diffuse %sDenoise %s %u", bChromaPass ? "Chroma " : "", direction == 0 ? "H" : "V", iteration);
                RenderPass& blur = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
                blur.ReadBuffer(SCENE_DATA_BUFFER);
                blur.ReadSampledImage(GI_GATHER_GUIDE);
                blur.ReadSampledImage(GI_GATHER_DATA);
                blur.ReadSampledImage(srcShR);
                blur.ReadSampledImage(srcShG);
                blur.ReadSampledImage(srcShB);
                if (bAO) {
                    blur.ReadSampledImage(SID("shadows_resolve_target"));
                }
                blur.WriteStorageImage(dstShR);
                blur.WriteStorageImage(dstShG);
                blur.WriteStorageImage(dstShB);
                if (!bChromaPass) {
                    blur.ReadSampledImage(srcSkyVis);
                    blur.WriteStorageImage(dstSkyVis);
                }

                blur.Execute([pipelineManager, sceneIndex, gatherExtent, renderExtent, gatherScale, direction, stepSize, srcShR, srcShG, srcShB, dstShR, dstShG, dstShB, srcSkyVis, dstSkyVis, bAO, bChromaPass, chromaLumaPower](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(bChromaPass ? SID("gi_denoise_chroma") : SID("gi_denoise"));
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
                        .aoIndex = bAO ? graph.GetSampledImageViewDescriptorIndex(SID("shadows_resolve_target")) : ~0x0u,
                        .chromaLumaPower = chromaLumaPower,
                        .srcSkyVisIndex = bChromaPass ? ~0x0u : graph.GetSampledImageViewDescriptorIndex(srcSkyVis),
                        .dstSkyVisIndex = bChromaPass ? ~0x0u : graph.GetStorageImageViewDescriptorIndex(dstSkyVis),
                        .gatherScale = gatherScale,
                    };
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, (gatherExtent[0] + 15u) / 16u, (gatherExtent[1] + 15u) / 16u, 1);
                });
            }
        }
    }

    graph.CreateTexture(GI_GATHER_RESOLVED, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_MOMENTS, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    SetupObjectMotion(graph, pipelineManager, renderExtent, targets, sceneIndex);

    const Core::Array<uint32_t, 2> motionTileExtent = {(renderExtent[0] + GI_MOTION_TILE_SIZE - 1u) / GI_MOTION_TILE_SIZE, (renderExtent[1] + GI_MOTION_TILE_SIZE - 1u) / GI_MOTION_TILE_SIZE};
    graph.CreateTexture(GI_MOTION_TILED_MAX, TextureInfo{VK_FORMAT_R16G16_SFLOAT, motionTileExtent[0], motionTileExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_MOTION_TILED_NEIGHBOR_MAX, TextureInfo{VK_FORMAT_R16G16_SFLOAT, motionTileExtent[0], motionTileExtent[1], 1}, {std::nullopt}, true);

    RenderPass& motionTileMax = graph.AddPass(SID("GI Motion Tile Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    motionTileMax.ReadSampledImage(OBJECT_MOTION);
    motionTileMax.WriteStorageImage(GI_MOTION_TILED_MAX);
    motionTileMax.Execute([pipelineManager, renderExtent, motionTileExtent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gi_motion_tile_max"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIMotionTileMaxPushConstant pc{
            .renderExtent = {renderExtent[0], renderExtent[1]},
            .objectMotionIndex = graph.GetSampledImageViewDescriptorIndex(OBJECT_MOTION),
            .tileMaxIndex = graph.GetStorageImageViewDescriptorIndex(GI_MOTION_TILED_MAX),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, motionTileExtent[0], motionTileExtent[1], 1);
    });

    RenderPass& motionNeighborMax = graph.AddPass(SID("GI Motion Neighbor Max"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    motionNeighborMax.ReadSampledImage(GI_MOTION_TILED_MAX);
    motionNeighborMax.WriteStorageImage(GI_MOTION_TILED_NEIGHBOR_MAX);
    motionNeighborMax.Execute([pipelineManager, motionTileExtent](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("motion_blur_neighbor_max"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        MotionBlurNeighborMaxPushConstant pc{
            .tileBufferSize = {motionTileExtent[0], motionTileExtent[1]},
            .tileMaxIndex = graph.GetSampledImageViewDescriptorIndex(GI_MOTION_TILED_MAX),
            .neighborMaxIndex = graph.GetStorageImageViewDescriptorIndex(GI_MOTION_TILED_NEIGHBOR_MAX),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (motionTileExtent[0] + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_X,
                      (motionTileExtent[1] + POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_CONVOLUTION_DISPATCH_Y, 1);
    });

    RenderPass& upscale = graph.AddPass(SID("GI Diffuse Upscale"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    upscale.ReadBuffer(SCENE_DATA_BUFFER);
    upscale.ReadSampledImage(targets.gbufferOne);
    upscale.ReadSampledImage(targets.depthCopy);
    upscale.ReadSampledImage(GI_GATHER_SH_R);
    upscale.ReadSampledImage(GI_GATHER_SH_G);
    upscale.ReadSampledImage(GI_GATHER_SH_B);
    upscale.ReadSampledImage(GI_GATHER_SKY_VIS);
    upscale.ReadSampledImage(GI_GATHER_VARIANCE_GUIDE);
    upscale.ReadSampledImage(GI_GATHER_DATA);
    upscale.ReadSampledImage(GI_GATHER_GUIDE);
    upscale.ReadSampledImage(GI_MOTION_TILED_NEIGHBOR_MAX);
    upscale.ReadBuffer(REFLECTION_PROBE_BUFFER);
    if (graph.HasBuffer(SID("world_grid_probe_grid"))) { upscale.ReadBuffer(SID("world_grid_probe_grid")); }
    if (bTemporal) {
        upscale.ReadSampledImage(GI_GATHER_HISTORY);
        upscale.ReadSampledImage(SID("depth_history"));
        upscale.ReadSampledImage(SID("gbuffer_one_history"));
    }
    const bool bMoments = bTemporal && graph.HasTexture(GI_GATHER_MOMENTS_HISTORY);
    if (bMoments) {
        upscale.ReadSampledImage(GI_GATHER_MOMENTS_HISTORY);
    }
    upscale.WriteStorageImage(GI_GATHER_MOMENTS);
    if (bAO) {
        upscale.ReadSampledImage(SID("shadows_resolve_target"));
    }
    const bool bBentNormals = graph.HasTexture(SID("gtao_bent_normals"));
    if (bBentNormals) {
        upscale.ReadSampledImage(SID("gtao_bent_normals"));
    }
    const bool bUpscaleCascades = AddDDGISampleDependencies(graph, upscale);
    upscale.WriteStorageImage(GI_GATHER_RESOLVED);
    if (bDebugUpscalePath) {
        graph.CreateTexture(GI_UPSCALE_PATH_DEBUG, TextureInfo{VK_FORMAT_R8G8B8A8_UNORM, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);
        upscale.WriteStorageImage(GI_UPSCALE_PATH_DEBUG);
    }

    upscale.Execute([pipelineManager, sceneIndex, gatherExtent, renderExtent, gatherScale, bTemporal, bMoments, bAO, bBentNormals, bUpscaleCascades, reflectionProbeCount, bProbeBrute, bDebugUpscalePath,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy,
            skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gi_upscale"));
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
            .historyIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_HISTORY) : ~0x0u,
            .depthHistoryIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(SID("depth_history")) : ~0x0u,
            .gbufferOneHistoryIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(SID("gbuffer_one_history")) : ~0x0u,
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_RESOLVED),
            .guideIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_GUIDE),
            .bHistoryValid = bTemporal ? 1u : 0u,
            .dataIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_DATA),
            .skyVisIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_SKY_VIS),
            .ddgiCascades = bUpscaleCascades ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
            .skyboxIndex = skyboxIndex,
            .iblIntensity = iblIntensity,
            .bCascadesValid = bUpscaleCascades ? 1u : 0u,
            .aoIndex = bAO ? graph.GetSampledImageViewDescriptorIndex(SID("shadows_resolve_target")) : ~0x0u,
            .bentNormalIndex = bBentNormals ? graph.GetSampledImageViewDescriptorIndex(SID("gtao_bent_normals")) : ~0x0u,
            .reflectionProbeCount = reflectionProbeCount,
            .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(REFLECTION_PROBE_BUFFER) : 0,
            .worldGridProbeGrid = (!bProbeBrute && graph.HasBuffer(SID("world_grid_probe_grid"))) ? graph.GetBufferAddress(SID("world_grid_probe_grid")) : 0,
            .momentsIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_MOMENTS),
            .momentsHistoryIndex = bMoments ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_MOMENTS_HISTORY) : ~0x0u,
            .bMomentsValid = bMoments ? 1u : 0u,
            .motionTileIndex = graph.GetSampledImageViewDescriptorIndex(GI_MOTION_TILED_NEIGHBOR_MAX),
            .varGuideIndex = graph.GetSampledImageViewDescriptorIndex(GI_GATHER_VARIANCE_GUIDE),
            .gatherScale = gatherScale,
            .debugPathIndex = bDebugUpscalePath ? graph.GetStorageImageViewDescriptorIndex(GI_UPSCALE_PATH_DEBUG) : ~0x0u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 15u) / 16u, (renderExtent[1] + 15u) / 16u, 1);
    });

    graph.CarryTextureToNextFrame(GI_GATHER_RESOLVED, GI_GATHER_HISTORY, VK_IMAGE_USAGE_SAMPLED_BIT);
    graph.CarryTextureToNextFrame(GI_GATHER_MOMENTS, GI_GATHER_MOMENTS_HISTORY, VK_IMAGE_USAGE_SAMPLED_BIT);

    return FinalGatherFrame{.bValid = true};
}

void SetupGIDeconstruct(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, int32_t mode)
{
    ZoneScoped;
    if (mode <= 0 || !graph.HasBuffer(SCENE_DATA_BUFFER) || !graph.HasBuffer(RADIANCE_CACHE_ENTRIES) || !graph.HasBuffer(RADIANCE_CACHE_KEYS) || !graph.HasBuffer(RADIANCE_CACHE_CELLS)) {
        return;
    }

    graph.CreateTexture(GI_DECONSTRUCT_TARGET, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass(SID("GI Deconstruct"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(RADIANCE_CACHE_ENTRIES);
    pass.ReadBuffer(RADIANCE_CACHE_KEYS);
    pass.ReadBuffer(RADIANCE_CACHE_CELLS);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    const bool bCascades = AddDDGISampleDependencies(graph, pass);
    pass.WriteStorageImage(GI_DECONSTRUCT_TARGET);
    pass.Execute([pipelineManager, sceneIndex, renderExtent, bCascades, mode, gbufferOne = targets.gbufferOne, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gi_deconstruct"));
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

    const bool bVarGuide = graph.HasTexture(GI_GATHER_VARIANCE_GUIDE);
    const bool bPath = graph.HasTexture(GI_UPSCALE_PATH_DEBUG);

    RenderPass& pass = graph.AddPass(SID("GI Gather Debug"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadSampledImage(GI_GATHER_RESOLVED);
    pass.ReadSampledImage(GI_GATHER_DATA);
    if (bVarGuide) {
        pass.ReadSampledImage(GI_GATHER_VARIANCE_GUIDE);
    }
    if (bPath) {
        pass.ReadSampledImage(GI_UPSCALE_PATH_DEBUG);
    }
    pass.WriteStorageImage(GI_GATHER_DEBUG_TARGET);
    pass.Execute([pipelineManager, renderExtent, mode, bVarGuide, bPath, bQuarterRes](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gi_gather_debug"));
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
            .varGuideIndex = bVarGuide ? graph.GetSampledImageViewDescriptorIndex(GI_GATHER_VARIANCE_GUIDE) : ~0x0u,
            .gatherScale = bQuarterRes ? 4u : 2u,
            .pathIndex = bPath ? graph.GetSampledImageViewDescriptorIndex(GI_UPSCALE_PATH_DEBUG) : ~0x0u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 15u) / 16u, (renderExtent[1] + 15u) / 16u, 1);
    });
}
} // Render
