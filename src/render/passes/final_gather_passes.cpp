//
// Created by William on 2026-07-12.
//

#include "render/passes/final_gather_passes.h"

#include "render/passes/ddgi_passes.h"
#include "render/render_utils.h"
#include "render/interface/render_interface.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
FinalGatherFrame SetupFinalGather(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Array<uint32_t, 2> renderExtent, const RenderTargets& targets, uint32_t sceneIndex, uint64_t frameNumber, bool bDenoise, bool bSkipRay, bool bDebugView)
{
    if (!graph.HasBuffer(RT_TLAS_BUFFER) || !graph.HasBuffer(SCENE_DATA_BUFFER) || !graph.HasBuffer(WORLD_CACHE_ENTRIES) || !graph.HasBuffer(WORLD_CACHE_CELLS)
        || !graph.HasBuffer(GEOMETRY_INSTANCE_BUFFER) || !graph.HasBuffer(GEOMETRY_PRIMITIVE_BUFFER) || !graph.HasBuffer(GEOMETRY_MODEL_BUFFER)
        || !graph.HasBuffer(GEOMETRY_MATERIAL_BUFFER) || !graph.HasBuffer(GEOMETRY_INDEX_BUFFER) || !graph.HasBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER)) {
        return {};
    }

    const Core::Array<uint32_t, 2> gatherExtent = {(renderExtent[0] + 1u) / 2u, (renderExtent[1] + 1u) / 2u};
    const StringID gatherShR = bDenoise ? GI_GATHER_RAW_SH_R : GI_GATHER_SH_R;
    const StringID gatherShG = bDenoise ? GI_GATHER_RAW_SH_G : GI_GATHER_SH_G;
    const StringID gatherShB = bDenoise ? GI_GATHER_RAW_SH_B : GI_GATHER_SH_B;
    graph.CreateTexture(gatherShR, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(gatherShG, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(gatherShB, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_DATA, TextureInfo{VK_FORMAT_R16G16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
    graph.CreateTexture(GI_GATHER_GUIDE, TextureInfo{VK_FORMAT_R32G32_UINT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);

    const bool bScreenSpace = !bDebugView && graph.HasTexture(SID("lit_color_history")) && graph.HasTexture(SID("depth_history")) && graph.HasTexture(SID("gbuffer_one_history"));

    const bool bExposure = viewFamily.postProcessConfig.bExposureEnabled && graph.HasBuffer(SID("luminance_buffer"));
    const float exposureTarget = bExposure ? viewFamily.postProcessConfig.exposureTargetLuminance : 0.0f;

    RenderPass& pass = graph.AddPass(SID("GI Diffuse Gather"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    pass.ReadTLASBuffer(RT_TLAS_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.ReadBuffer(WORLD_CACHE_ENTRIES);
    pass.ReadWriteBuffer(WORLD_CACHE_CELLS);
    pass.ReadBuffer(GEOMETRY_INSTANCE_BUFFER);
    pass.ReadBuffer(GEOMETRY_PRIMITIVE_BUFFER);
    pass.ReadBuffer(GEOMETRY_MODEL_BUFFER);
    pass.ReadBuffer(GEOMETRY_MATERIAL_BUFFER);
    pass.ReadBuffer(GEOMETRY_INDEX_BUFFER);
    pass.ReadBuffer(GEOMETRY_VERTEX_ATTRIBUTE_BUFFER);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    if (bScreenSpace) {
        pass.ReadSampledImage(SID("lit_color_history"));
        pass.ReadSampledImage(SID("depth_history"));
        pass.ReadSampledImage(SID("gbuffer_one_history"));
    }
    if (bExposure) {
        pass.ReadBuffer(SID("luminance_buffer"));
    }
    const bool bCascades = AddDDGISampleDependencies(graph, pass);
    pass.WriteStorageImage(gatherShR);
    pass.WriteStorageImage(gatherShG);
    pass.WriteStorageImage(gatherShB);
    pass.WriteStorageImage(GI_GATHER_DATA);
    pass.WriteStorageImage(GI_GATHER_GUIDE);

    pass.Execute([pipelineManager, sceneIndex, frameNumber, gatherExtent, renderExtent, bCascades, bScreenSpace, bSkipRay, gatherShR, gatherShG, gatherShB, bExposure, exposureTarget,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy,
            skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gi_gather"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIGatherPushConstant pc{
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
            .ddgiCascades = bCascades ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
            .cacheEntries = graph.GetBufferAddress(WORLD_CACHE_ENTRIES),
            .cacheCells = graph.GetBufferAddress(WORLD_CACHE_CELLS),
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
            .exposureLuminance = bExposure ? graph.GetBufferAddress(SID("luminance_buffer")) : 0,
            .exposureTarget = exposureTarget,
            .guideOutIndex = graph.GetStorageImageViewDescriptorIndex(GI_GATHER_GUIDE),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (gatherExtent[0] + 7u) / 8u, (gatherExtent[1] + 7u) / 8u, 1);
    });

    const bool bTemporal = graph.HasTexture(GI_GATHER_HISTORY) && graph.HasTexture(SID("depth_history")) && graph.HasTexture(SID("gbuffer_one_history"));
    const bool bAO = graph.HasTexture(SID("shadows_resolve_target"));

    if (bDenoise) {
        graph.CreateTexture(GI_GATHER_TMP_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_TMP_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_TMP_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);
        graph.CreateTexture(GI_GATHER_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent[0], gatherExtent[1], 1}, {std::nullopt}, true);

        constexpr uint32_t denoiseStrides[] = {1u, 2u, 4u};
        for (uint32_t iteration = 0; iteration < 3; iteration++) {
            const uint32_t stepSize = denoiseStrides[iteration];
            for (uint32_t direction = 0; direction < 2; direction++) {
                const StringID srcShR = direction != 0 ? GI_GATHER_TMP_SH_R : (iteration == 0 ? GI_GATHER_RAW_SH_R : GI_GATHER_SH_R);
                const StringID srcShG = direction != 0 ? GI_GATHER_TMP_SH_G : (iteration == 0 ? GI_GATHER_RAW_SH_G : GI_GATHER_SH_G);
                const StringID srcShB = direction != 0 ? GI_GATHER_TMP_SH_B : (iteration == 0 ? GI_GATHER_RAW_SH_B : GI_GATHER_SH_B);
                const StringID dstShR = direction == 0 ? GI_GATHER_TMP_SH_R : GI_GATHER_SH_R;
                const StringID dstShG = direction == 0 ? GI_GATHER_TMP_SH_G : GI_GATHER_SH_G;
                const StringID dstShB = direction == 0 ? GI_GATHER_TMP_SH_B : GI_GATHER_SH_B;

                const Core::InlineString<32> passName = Core::InlineString<32>::Format("GI Diffuse Denoise %s %u", direction == 0 ? "H" : "V", iteration);
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
                if (bExposure) {
                    blur.ReadBuffer(SID("luminance_buffer"));
                }
                blur.WriteStorageImage(dstShR);
                blur.WriteStorageImage(dstShG);
                blur.WriteStorageImage(dstShB);

                blur.Execute([pipelineManager, sceneIndex, gatherExtent, renderExtent, direction, stepSize, srcShR, srcShG, srcShB, dstShR, dstShG, dstShB, bAO, bExposure, exposureTarget](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gi_denoise"));
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
                        .exposureLuminance = bExposure ? graph.GetBufferAddress(SID("luminance_buffer")) : 0,
                        .exposureTarget = exposureTarget,
                    };
                    vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
                    vkCmdDispatch(cmd, (gatherExtent[0] + 15u) / 16u, (gatherExtent[1] + 15u) / 16u, 1);
                });
            }
        }
    }

    graph.CreateTexture(GI_GATHER_RESOLVED, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent[0], renderExtent[1], 1}, {std::nullopt}, true);

    RenderPass& upscale = graph.AddPass(SID("GI Diffuse Upscale"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    upscale.ReadBuffer(SCENE_DATA_BUFFER);
    upscale.ReadSampledImage(targets.gbufferOne);
    upscale.ReadSampledImage(targets.depthCopy);
    upscale.ReadSampledImage(GI_GATHER_SH_R);
    upscale.ReadSampledImage(GI_GATHER_SH_G);
    upscale.ReadSampledImage(GI_GATHER_SH_B);
    upscale.ReadSampledImage(GI_GATHER_DATA);
    upscale.ReadSampledImage(GI_GATHER_GUIDE);
    if (bTemporal) {
        upscale.ReadSampledImage(GI_GATHER_HISTORY);
        upscale.ReadSampledImage(SID("depth_history"));
        upscale.ReadSampledImage(SID("gbuffer_one_history"));
    }
    if (bAO) {
        upscale.ReadSampledImage(SID("shadows_resolve_target"));
    }
    const bool bBentNormals = graph.HasTexture(SID("gtao_bent_normals"));
    if (bBentNormals) {
        upscale.ReadSampledImage(SID("gtao_bent_normals"));
    }
    if (bExposure) {
        upscale.ReadBuffer(SID("luminance_buffer"));
    }
    const bool bUpscaleCascades = AddDDGISampleDependencies(graph, upscale);
    upscale.WriteStorageImage(GI_GATHER_RESOLVED);

    upscale.Execute([pipelineManager, sceneIndex, gatherExtent, renderExtent, bTemporal, bAO, bBentNormals, bUpscaleCascades, bExposure, exposureTarget,
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
            .ddgiCascades = bUpscaleCascades ? graph.GetBufferAddress(DDGI_CASCADES_BUFFER) : 0,
            .skyboxIndex = skyboxIndex,
            .iblIntensity = iblIntensity,
            .bCascadesValid = bUpscaleCascades ? 1u : 0u,
            .aoIndex = bAO ? graph.GetSampledImageViewDescriptorIndex(SID("shadows_resolve_target")) : ~0x0u,
            .bentNormalIndex = bBentNormals ? graph.GetSampledImageViewDescriptorIndex(SID("gtao_bent_normals")) : ~0x0u,
            .exposureLuminance = bExposure ? graph.GetBufferAddress(SID("luminance_buffer")) : 0,
            .exposureTarget = exposureTarget,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent[0] + 15u) / 16u, (renderExtent[1] + 15u) / 16u, 1);
    });

    graph.CarryTextureToNextFrame(GI_GATHER_RESOLVED, GI_GATHER_HISTORY, VK_IMAGE_USAGE_SAMPLED_BIT);

    return FinalGatherFrame{.bValid = true};
}
} // Render
