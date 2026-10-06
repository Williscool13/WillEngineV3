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
RDGTexture SetupObjectMotion(RenderGraph& graph, PipelineManager* pipelineManager, Core::Extent2D renderExtent, const SceneResources& scene, const RenderTargets& targets, uint32_t sceneIndex)
{
    ZoneScoped;
    const RDGTexture objectMotion = graph.CreateTexture(OBJECT_MOTION, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass("Object Motion Extract"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Untagged);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    pass.WriteStorageImage(objectMotion);
    pass.Execute([&scene, pipelineManager, sceneIndex, renderExtent, objectMotion, gbufferOne = targets.gbufferOne, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("object_motion_extract"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ObjectMotionExtractPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .renderExtent = {renderExtent.width, renderExtent.height},
            .sceneDataIndex = sceneIndex,
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(objectMotion),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent.width + POST_PROCESS_MOTION_BLUR_DISPATCH_X - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_X, (renderExtent.height + POST_PROCESS_MOTION_BLUR_DISPATCH_Y - 1) / POST_PROCESS_MOTION_BLUR_DISPATCH_Y, 1);
    });

    return objectMotion;
}

FinalGatherFrame SetupFinalGather(RenderGraph& graph, PipelineManager* pipelineManager, const Core::ViewFamily& viewFamily, Core::Extent2D renderExtent, const SceneResources& scene, const RenderTargets& targets,
    const RadianceCacheFrame& radianceCache, const DDGIFrame& ddgi, const WorldGridFrame& worldGrid, const GTAOFrame& gtao, uint32_t sceneIndex, uint64_t frameNumber,
    bool bDenoise, bool bTemporalFilter, uint32_t raysPerPixel, bool bDebugView, bool bDisableScreenTier, bool bQuarterRes, float bounceIntensity, float maxRayRadiance)
{
    ZoneScoped;
    if (!scene.tlas.IsValid() || !scene.sceneData.IsValid() || !radianceCache.entries.IsValid() || !radianceCache.cells.IsValid()
        || !scene.instances.IsValid() || !scene.primitives.IsValid() || !scene.models.IsValid()
        || !scene.materials.IsValid() || !scene.indices.IsValid() || !scene.vertexAttributes.IsValid()) {
        return {};
    }

    const uint32_t gatherScale = bQuarterRes ? 4u : 2u;
    const Core::Extent2D gatherExtent = {(renderExtent.width + gatherScale - 1u) / gatherScale, (renderExtent.height + gatherScale - 1u) / gatherScale};
    const RDGTexture gatherShR = graph.CreateTexture(bDenoise ? GI_GATHER_RAW_SH_R : GI_GATHER_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
    const RDGTexture gatherShG = graph.CreateTexture(bDenoise ? GI_GATHER_RAW_SH_G : GI_GATHER_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
    const RDGTexture gatherShB = graph.CreateTexture(bDenoise ? GI_GATHER_RAW_SH_B : GI_GATHER_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
    const RDGTexture gatherSkyVis = graph.CreateTexture(bDenoise ? GI_GATHER_RAW_SKY_VIS : GI_GATHER_SKY_VIS, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
    const RDGTexture gatherData = graph.CreateTexture(GI_GATHER_DATA, TextureInfo{VK_FORMAT_R16G16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
    const RDGTexture gatherGuide = graph.CreateTexture(GI_GATHER_GUIDE, TextureInfo{VK_FORMAT_R32G32_UINT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
    const RDGTextureRing historyRing = graph.CreateVersionedTexture(GI_GATHER_HISTORY, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTexture gatherHistory = historyRing.Version(1);
    const RDGTexture gatherResolved = graph.CreateTexture(GI_GATHER_RESOLVED, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
    const RDGTexture guideNormal = graph.CreateTexture(GI_GATHER_GUIDE_NORMAL, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
    const RDGTextureRing fastRing = graph.CreateVersionedTexture(GI_GATHER_FAST, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTexture fastHistory = fastRing.Version(1);
    const RDGTextureRing skyVisRing = graph.CreateVersionedTexture(GI_GATHER_SKY_VIS_HISTORY, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTexture skyVisHistory = skyVisRing.Version(1);
    const RDGTextureRing noiseRing = graph.CreateVersionedTexture(GI_GATHER_NOISE, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTexture noiseHistory = noiseRing.Version(1);

    const RDGTexture litHistory = targets.litSnapshotHistory;
    const RDGTexture depthHistory = targets.depthCopyHistory;
    const RDGTexture gbufferOneHistory = targets.gbufferOneHistory;

    const bool bScreenSpace = !bDebugView && !bDisableScreenTier && litHistory.IsValid() && depthHistory.IsValid() && gbufferOneHistory.IsValid();
    const bool bScreenDiffuse = bScreenSpace && targets.giScreenDiffuseHistory.IsValid();
    const RDGTexture screenDiffuseHistory = targets.giScreenDiffuseHistory;

    const uint32_t gatherRayCount = glm::clamp(raysPerPixel, 1u, GI_GATHER_MAX_RAYS_PER_PIXEL);
    const RDGBuffer gatherHits = graph.CreateBuffer("gi_gather_hits"_sid, static_cast<VkDeviceSize>(gatherExtent.width) * gatherExtent.height * gatherRayCount * sizeof(GIGatherHit), true);

    RenderPass& tracePass = graph.AddPass("GI Diffuse Gather Trace"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    tracePass.ReadTLASBuffer(scene.tlas);
    tracePass.ReadBuffer(scene.sceneData);
    tracePass.ReadSampledImage(targets.gbufferOne);
    tracePass.ReadSampledImage(targets.depthCopy);
    tracePass.WriteBuffer(gatherHits);
    tracePass.Execute([&scene, pipelineManager, sceneIndex, gatherHits, frameNumber, gatherExtent, renderExtent, gatherScale, gatherRayCount,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_gather_trace"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIGatherPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .gatherExtent = {gatherExtent.width, gatherExtent.height},
            .renderExtent = {renderExtent.width, renderExtent.height},
            .sceneDataIndex = sceneIndex,
            .tlasIndex = graph.GetAccelerationStructureDescriptorIndex(scene.tlas),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .rayCount = gatherRayCount,
            .gatherScale = gatherScale,
            .hitBuffer = graph.GetBufferAddress(gatherHits),
            .screenDiffuseHistoryIndex = ~0x0u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (gatherExtent.width + 7) / 8, (gatherExtent.height + 7) / 8, 1);
    });

    RenderPass& pass = graph.AddPass("GI Diffuse Gather Shade"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(radianceCache.entries);
    pass.ReadBuffer(radianceCache.keys);
    pass.ReadBuffer(radianceCache.cells);
    const RDGBuffer touchEntries = radianceCache.touchEntries;
    const bool bTouch = touchEntries.IsValid();
    if (bTouch) {
        pass.ReadWriteBuffer(touchEntries);
    }
    pass.ReadBuffer(scene.instances);
    pass.ReadBuffer(scene.primitives);
    pass.ReadBuffer(scene.models);
    pass.ReadBuffer(scene.materials);
    pass.ReadBuffer(scene.indices);
    pass.ReadBuffer(scene.vertexAttributes);
    pass.ReadBuffer(scene.reflectionProbes);
    const RDGBuffer probeGrid = worldGrid.probeGrid;
    if (probeGrid.IsValid()) { pass.ReadBuffer(probeGrid); }
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
    const bool bCascades = AddDDGISampleDependencies(graph, pass, ddgi);
    pass.ReadBuffer(gatherHits);
    pass.WriteStorageImage(gatherShR);
    pass.WriteStorageImage(gatherShG);
    pass.WriteStorageImage(gatherShB);
    pass.WriteStorageImage(gatherSkyVis);
    pass.WriteStorageImage(gatherData);
    pass.WriteStorageImage(gatherGuide);

    const uint32_t reflectionProbeCount = static_cast<uint32_t>(viewFamily.reflectionProbes.Size());
    const bool bProbeBrute = viewFamily.bReflectionProbeBruteForce;
    pass.Execute([&scene, pipelineManager, sceneIndex, frameNumber, gatherExtent, renderExtent, gatherScale, gatherRayCount, bCascades, bScreenSpace, gatherShR, gatherShG, gatherShB, gatherSkyVis, gatherData, gatherGuide,
            gatherHits, reflectionProbeCount, bProbeBrute, bTouch, touchEntries, probeGrid, litHistory, depthHistory, gbufferOneHistory, bScreenDiffuse, screenDiffuseHistory,
            cacheEntries = radianceCache.entries, cacheKeys = radianceCache.keys, cacheCells = radianceCache.cells, ddgiCascades = ddgi.cascades,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy,
            skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity, bounceIntensity = glm::clamp(bounceIntensity, 0.0f, 1.0f), maxRayRadiance = glm::max(maxRayRadiance, 0.0f)](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_gather_shade"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIGatherPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .ddgiCascades = bCascades ? graph.GetBufferAddress(ddgiCascades) : 0,
            .cacheEntries = graph.GetBufferAddress(cacheEntries),
            .cacheKeys = graph.GetBufferAddress(cacheKeys),
            .cacheCells = graph.GetBufferAddress(cacheCells),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .indexBuffer = graph.GetBufferAddress(scene.indices),
            .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
            .gatherExtent = {gatherExtent.width, gatherExtent.height},
            .renderExtent = {renderExtent.width, renderExtent.height},
            .sceneDataIndex = sceneIndex,
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .shRIndex = graph.GetStorageImageViewDescriptorIndex(gatherShR),
            .shGIndex = graph.GetStorageImageViewDescriptorIndex(gatherShG),
            .shBIndex = graph.GetStorageImageViewDescriptorIndex(gatherShB),
            .dataIndex = graph.GetStorageImageViewDescriptorIndex(gatherData),
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .skyboxIndex = skyboxIndex,
            .iblIntensity = iblIntensity,
            .bCascadesValid = bCascades ? 1u : 0u,
            .litHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(litHistory) : ~0x0u,
            .depthHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0x0u,
            .gbufferOneHistoryIndex = bScreenSpace ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0x0u,
            .guideOutIndex = graph.GetStorageImageViewDescriptorIndex(gatherGuide),
            .reflectionProbeCount = reflectionProbeCount,
            .skyVisIndex = graph.GetStorageImageViewDescriptorIndex(gatherSkyVis),
            .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
            .worldGridProbeGrid = (!bProbeBrute && probeGrid.IsValid()) ? graph.GetBufferAddress(probeGrid) : 0,
            .rayCount = gatherRayCount,
            .gatherScale = gatherScale,
            .touchEntries = bTouch ? graph.GetBufferAddress(touchEntries) : 0,
            .hitBuffer = graph.GetBufferAddress(gatherHits),
            .bounceIntensity = bounceIntensity,
            .screenDiffuseHistoryIndex = bScreenDiffuse ? graph.GetSampledImageViewDescriptorIndex(screenDiffuseHistory) : ~0x0u,
            .maxRayRadiance = maxRayRadiance,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (gatherExtent.width + 7u) / 8u, (gatherExtent.height + 7u) / 8u, 1);
    });

    const bool bTemporal = bTemporalFilter && gatherHistory.IsValid() && depthHistory.IsValid() && gbufferOneHistory.IsValid();

    RDGTexture shR = gatherShR;
    RDGTexture shG = gatherShG;
    RDGTexture shB = gatherShB;
    RDGTexture skyVis = gatherSkyVis;

    if (bDenoise) {
        const RDGTexture tmpShR = graph.CreateTexture(GI_GATHER_TMP_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
        const RDGTexture tmpShG = graph.CreateTexture(GI_GATHER_TMP_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
        const RDGTexture tmpShB = graph.CreateTexture(GI_GATHER_TMP_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
        shR = graph.CreateTexture(GI_GATHER_SH_R, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
        shG = graph.CreateTexture(GI_GATHER_SH_G, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
        shB = graph.CreateTexture(GI_GATHER_SH_B, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
        const RDGTexture tmpSkyVis = graph.CreateTexture(GI_GATHER_TMP_SKY_VIS, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);
        skyVis = graph.CreateTexture(GI_GATHER_SKY_VIS, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, gatherExtent.width, gatherExtent.height, 1}, {std::nullopt}, true);

        constexpr uint32_t denoiseStrides[] = {1u, 2u, 4u};
        for (uint32_t iteration = 0; iteration < 3u; iteration++) {
            const uint32_t stepSize = denoiseStrides[iteration];
            for (uint32_t direction = 0; direction < 2; direction++) {
                const RDGTexture srcShR = direction != 0 ? tmpShR : (iteration == 0 ? gatherShR : shR);
                const RDGTexture srcShG = direction != 0 ? tmpShG : (iteration == 0 ? gatherShG : shG);
                const RDGTexture srcShB = direction != 0 ? tmpShB : (iteration == 0 ? gatherShB : shB);
                const RDGTexture dstShR = direction == 0 ? tmpShR : shR;
                const RDGTexture dstShG = direction == 0 ? tmpShG : shG;
                const RDGTexture dstShB = direction == 0 ? tmpShB : shB;
                const RDGTexture srcSkyVis = direction != 0 ? tmpSkyVis : (iteration == 0 ? gatherSkyVis : skyVis);
                const RDGTexture dstSkyVis = direction == 0 ? tmpSkyVis : skyVis;

                const Core::InlineString<40> passName = Core::InlineString<40>::Format("GI Diffuse Denoise %s %u", direction == 0 ? "H" : "V", iteration);
                RenderPass& blur = graph.AddPass(StringID(passName.c_str(), passName.Size()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
                blur.ReadBuffer(scene.sceneData);
                blur.ReadSampledImage(gatherGuide);
                blur.ReadSampledImage(gatherData);
                blur.ReadSampledImage(srcShR);
                blur.ReadSampledImage(srcShG);
                blur.ReadSampledImage(srcShB);
                blur.ReadSampledImage(srcSkyVis);
                blur.WriteStorageImage(dstShR);
                blur.WriteStorageImage(dstShG);
                blur.WriteStorageImage(dstShB);
                blur.WriteStorageImage(dstSkyVis);

                blur.Execute([&scene, pipelineManager, sceneIndex, gatherExtent, renderExtent, gatherScale, direction, stepSize, gatherGuide, gatherData, srcShR, srcShG, srcShB, dstShR, dstShG, dstShB, srcSkyVis, dstSkyVis](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
                    const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_denoise"_sid);
                    if (!pipelineEntry) {
                        return;
                    }
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

                    GIDenoisePushConstant pc{
                        .sceneData = graph.GetBufferAddress(scene.sceneData),
                        .gatherExtent = {gatherExtent.width, gatherExtent.height},
                        .renderExtent = {renderExtent.width, renderExtent.height},
                        .sceneDataIndex = sceneIndex,
                        .guideIndex = graph.GetSampledImageViewDescriptorIndex(gatherGuide),
                        .dataIndex = graph.GetSampledImageViewDescriptorIndex(gatherData),
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
                    vkCmdDispatch(cmd, (gatherExtent.width + 15u) / 16u, (gatherExtent.height + 15u) / 16u, 1);
                });
            }
        }
    }

    RenderPass& upscale = graph.AddPass("GI Diffuse Upscale"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    upscale.ReadBuffer(scene.sceneData);
    upscale.ReadSampledImage(targets.gbufferOne);
    upscale.ReadSampledImage(targets.depthCopy);
    upscale.ReadSampledImage(shR);
    upscale.ReadSampledImage(shG);
    upscale.ReadSampledImage(shB);
    upscale.ReadSampledImage(skyVis);
    upscale.ReadSampledImage(gatherData);
    upscale.ReadSampledImage(gatherGuide);
    upscale.ReadBuffer(scene.reflectionProbes);
    if (probeGrid.IsValid()) { upscale.ReadBuffer(probeGrid); }
    const bool bFastHistory = bTemporal && fastHistory.IsValid();
    const bool bSkyVisHistory = bTemporal && skyVisHistory.IsValid();
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
    const bool bNoiseHistory = bTemporal && noiseHistory.IsValid();
    if (bNoiseHistory) {
        upscale.ReadSampledImage(noiseHistory);
    }
    const RDGTexture bentNormals = gtao.bentNormals;
    const bool bBentNormals = bentNormals.IsValid();
    if (bBentNormals) {
        upscale.ReadSampledImage(bentNormals);
    }
    const bool bUpscaleCascades = AddDDGISampleDependencies(graph, upscale, ddgi);
    const RDGTexture historyOut = historyRing.Current();
    const RDGTexture fastOut = fastRing.Current();
    const RDGTexture skyVisOut = skyVisRing.Current();
    const RDGTexture noiseOut = noiseRing.Current();
    upscale.WriteStorageImage(historyOut);
    upscale.WriteStorageImage(fastOut);
    upscale.WriteStorageImage(skyVisOut);
    upscale.WriteStorageImage(noiseOut);
    upscale.WriteStorageImage(guideNormal);

    upscale.Execute([&scene, pipelineManager, sceneIndex, gatherExtent, renderExtent, gatherScale, bTemporal, bFastHistory, bSkyVisHistory, bNoiseHistory, bBentNormals, bUpscaleCascades, reflectionProbeCount, bProbeBrute, gatherHistory, fastHistory, skyVisHistory, noiseHistory, depthHistory, gbufferOneHistory,
            shR, shG, shB, skyVis, gatherData, gatherGuide, bentNormals, probeGrid, historyOut, fastOut, skyVisOut, noiseOut, guideNormal, ddgiCascades = ddgi.cascades,
            gbufferOne = targets.gbufferOne, depth = targets.depthCopy,
            skyboxIndex = viewFamily.skyboxIndex, iblIntensity = viewFamily.iblIntensity](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_upscale"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIUpscalePushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .gatherExtent = {gatherExtent.width, gatherExtent.height},
            .renderExtent = {renderExtent.width, renderExtent.height},
            .sceneDataIndex = sceneIndex,
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .shRIndex = graph.GetSampledImageViewDescriptorIndex(shR),
            .shGIndex = graph.GetSampledImageViewDescriptorIndex(shG),
            .shBIndex = graph.GetSampledImageViewDescriptorIndex(shB),
            .historyIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(gatherHistory) : ~0x0u,
            .depthHistoryIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0x0u,
            .gbufferOneHistoryIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0x0u,
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(historyOut),
            .guideIndex = graph.GetSampledImageViewDescriptorIndex(gatherGuide),
            .bHistoryValid = bTemporal ? 1u : 0u,
            .dataIndex = graph.GetSampledImageViewDescriptorIndex(gatherData),
            .skyVisIndex = graph.GetSampledImageViewDescriptorIndex(skyVis),
            .ddgiCascades = bUpscaleCascades ? graph.GetBufferAddress(ddgiCascades) : 0,
            .skyboxIndex = skyboxIndex,
            .iblIntensity = iblIntensity,
            .bCascadesValid = bUpscaleCascades ? 1u : 0u,
            .bentNormalIndex = bBentNormals ? graph.GetSampledImageViewDescriptorIndex(bentNormals) : ~0x0u,
            .reflectionProbeCount = reflectionProbeCount,
            .gatherScale = gatherScale,
            .reflectionProbes = reflectionProbeCount > 0u ? graph.GetBufferAddress(scene.reflectionProbes) : 0,
            .worldGridProbeGrid = (!bProbeBrute && probeGrid.IsValid()) ? graph.GetBufferAddress(probeGrid) : 0,
            .fastHistoryIndex = bFastHistory ? graph.GetSampledImageViewDescriptorIndex(fastHistory) : ~0x0u,
            .fastOutIndex = graph.GetStorageImageViewDescriptorIndex(fastOut),
            .skyVisHistoryIndex = bSkyVisHistory ? graph.GetSampledImageViewDescriptorIndex(skyVisHistory) : ~0x0u,
            .skyVisOutIndex = graph.GetStorageImageViewDescriptorIndex(skyVisOut),
            .noiseHistoryIndex = bNoiseHistory ? graph.GetSampledImageViewDescriptorIndex(noiseHistory) : ~0x0u,
            .noiseOutIndex = graph.GetStorageImageViewDescriptorIndex(noiseOut),
            .guideNormalOutIndex = graph.GetStorageImageViewDescriptorIndex(guideNormal),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent.width + 15u) / 16u, (renderExtent.height + 15u) / 16u, 1);
    });

    RenderPass& postBlur = graph.AddPass("GI Diffuse Post Blur"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::FinalGather);
    postBlur.ReadBuffer(scene.sceneData);
    postBlur.ReadSampledImage(historyOut);
    postBlur.ReadSampledImage(noiseOut);
    postBlur.ReadSampledImage(guideNormal);
    postBlur.ReadSampledImage(targets.depthCopy);
    postBlur.WriteStorageImage(gatherResolved);
    postBlur.Execute([&scene, pipelineManager, sceneIndex, renderExtent, gatherScale, frameNumber, historyOut, noiseOut, guideNormal, gatherResolved, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_post_blur"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIPostBlurPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .renderExtent = {renderExtent.width, renderExtent.height},
            .sceneDataIndex = sceneIndex,
            .inputIndex = graph.GetSampledImageViewDescriptorIndex(historyOut),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(gatherResolved),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .guideNormalIndex = graph.GetSampledImageViewDescriptorIndex(guideNormal),
            .gatherScale = gatherScale,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .noiseIndex = graph.GetSampledImageViewDescriptorIndex(noiseOut),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent.width + 15u) / 16u, (renderExtent.height + 15u) / 16u, 1);
    });

    return FinalGatherFrame{.data = gatherData, .resolved = gatherResolved, .skyVisHistory = skyVisOut};
}

void SetupGIDeconstruct(RenderGraph& graph, PipelineManager* pipelineManager, Core::Extent2D renderExtent, const SceneResources& scene, const RenderTargets& targets, const RadianceCacheFrame& radianceCache,
    const DDGIFrame& ddgi, uint32_t sceneIndex, int32_t mode)
{
    ZoneScoped;
    if (mode <= 0 || !scene.sceneData.IsValid() || !radianceCache.entries.IsValid() || !radianceCache.keys.IsValid() || !radianceCache.cells.IsValid()) {
        return;
    }

    const RDGTexture output = graph.CreateTexture(GI_DECONSTRUCT_TARGET, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass("GI Deconstruct"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(radianceCache.entries);
    pass.ReadBuffer(radianceCache.keys);
    pass.ReadBuffer(radianceCache.cells);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(targets.depthCopy);
    const bool bCascades = AddDDGISampleDependencies(graph, pass, ddgi);
    pass.WriteStorageImage(output);
    pass.Execute([&scene, pipelineManager, sceneIndex, renderExtent, bCascades, mode, output, ddgiCascades = ddgi.cascades, cacheEntries = radianceCache.entries, cacheKeys = radianceCache.keys,
            cacheCells = radianceCache.cells, gbufferOne = targets.gbufferOne, depth = targets.depthCopy](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_deconstruct"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIDeconstructPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .ddgiCascades = bCascades ? graph.GetBufferAddress(ddgiCascades) : 0,
            .cacheEntries = graph.GetBufferAddress(cacheEntries),
            .cacheKeys = graph.GetBufferAddress(cacheKeys),
            .cacheCells = graph.GetBufferAddress(cacheCells),
            .renderExtent = {renderExtent.width, renderExtent.height},
            .sceneDataIndex = sceneIndex,
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .mode = static_cast<uint32_t>(mode),
            .bCascadesValid = bCascades ? 1u : 0u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent.width + 15u) / 16u, (renderExtent.height + 15u) / 16u, 1);
    });
}

void SetupGIGatherDebug(RenderGraph& graph, PipelineManager* pipelineManager, Core::Extent2D renderExtent, const FinalGatherFrame& gather, int32_t mode, bool bQuarterRes)
{
    ZoneScoped;
    if (mode <= 0 || !gather.resolved.IsValid() || !gather.data.IsValid()) {
        return;
    }

    const RDGTexture output = graph.CreateTexture(GI_GATHER_DEBUG_TARGET, TextureInfo{VK_FORMAT_R16G16B16A16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass("GI Gather Debug"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadSampledImage(gather.resolved);
    pass.ReadSampledImage(gather.data);
    pass.WriteStorageImage(output);
    pass.Execute([pipelineManager, renderExtent, mode, bQuarterRes, output, resolved = gather.resolved, data = gather.data](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gi_gather_debug"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        GIGatherDebugPushConstant pc{
            .renderExtent = {renderExtent.width, renderExtent.height},
            .resolvedIndex = graph.GetSampledImageViewDescriptorIndex(resolved),
            .dataIndex = graph.GetSampledImageViewDescriptorIndex(data),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            // GIGatherDebugColor keeps the composite-era 2-based numbering.
            .mode = static_cast<uint32_t>(mode) + 1u,
            .gatherScale = bQuarterRes ? 4u : 2u,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent.width + 15u) / 16u, (renderExtent.height + 15u) / 16u, 1);
    });
}
} // Render
