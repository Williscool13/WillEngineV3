//
// Created by William on 2026-06-03.
//

#include "render/passes/shadow_passes.h"

#include <tracy/Tracy.hpp>

#include "render/render_utils.h"
#include "render/passes/geometry_passes.h"
#include "render/render-view/csm_views.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"
#include "render/vulkan/vk_config.h"

namespace Render
{
RDGTexture SetupCSMDepth(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         const SceneBufferSizes& bufferSizes,
                         const SceneResources& scene,
                         RDGBuffer csmData,
                         uint32_t cascadeCount,
                         uint32_t sceneIndex)
{
    ZoneScoped;
    if (viewFamily.instanceCount == 0) {
        return {};
    }

    constexpr const char* PREFIX = "[CSM]";
    constexpr RenderCategory CATEGORY = RenderCategory::ShadowMaps;
    const uint32_t instanceCount = viewFamily.instanceCount;
    const uint32_t elementCount = instanceCount * cascadeCount;
    const uint32_t meshletUpperBound = bufferSizes.shadowCull.visibleMeshletUpperBound;
    const auto lodBias = static_cast<int32_t>(LOD_BIAS);

    const glm::uvec2 atlasExtent = CSMAtlasExtent(cascadeCount, static_cast<uint32_t>(viewFamily.csm.resolution));
    const RDGTexture atlas = graph.CreateTexture("csm_atlas"_sid, TextureInfo{CSM_DEPTH_FORMAT, atlasExtent.x, atlasExtent.y, 1}, CLEAR_DEPTH_FAR);

    const MeshletCullBuffers cull = CreateMeshletCullBuffers(graph, bufferSizes.shadowCull, "csm_");
    const RDGBuffer instanceMeshletOffsets = cull.instanceMeshletOffsets;
    const RDGBuffer intermediateMeshlets = cull.intermediateMeshlets;
    const RDGBuffer visibleMeshlets = cull.visibleMeshlets;
    const RDGBuffer meshletCountDispatchArgs = cull.meshletCountDispatchArgs;
    const RDGBuffer compactedMeshletDispatchArgs = cull.compactedMeshletDispatchArgs;

    AddMeshletCullClear(graph, cull, PREFIX, CATEGORY);

    RenderPass& instanceCull = graph.AddPass("[CSM] Instance Cull"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, CATEGORY);
    instanceCull.ReadBuffer(scene.sceneData);
    instanceCull.ReadBuffer(csmData);
    instanceCull.ReadBuffer(scene.primitives);
    instanceCull.ReadBuffer(scene.models);
    instanceCull.ReadBuffer(scene.instances);
    instanceCull.WriteBuffer(instanceMeshletOffsets);
    instanceCull.Execute([&scene, pipelineManager, csmData, instanceMeshletOffsets, instanceCount, cascadeCount, elementCount, sceneIndex, lodBias](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("csm_instance_cull"_sid);
        CSMInstanceCullPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .csmData = graph.GetBufferAddress(csmData),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
            .instanceCount = instanceCount,
            .cascadeCount = cascadeCount,
            .sceneDataIndex = sceneIndex,
            .lodBias = lodBias,
        };
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (elementCount + INSTANCING_VISIBILITY_DISPATCH_X - 1) / INSTANCING_VISIBILITY_DISPATCH_X, 1, 1);
    });

    AddInstanceMeshletPrefixSum(graph, pipelineManager, cull, elementCount, PREFIX, CATEGORY);

    RenderPass& expand = graph.AddPass("[CSM] Expand Instance To Meshlet"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, CATEGORY);
    expand.ReadBuffer(csmData);
    expand.ReadBuffer(scene.instances);
    expand.ReadBuffer(scene.primitives);
    expand.ReadBuffer(scene.models);
    expand.ReadBuffer(scene.meshlets);
    expand.ReadBuffer(scene.materials);
    expand.ReadBuffer(instanceMeshletOffsets);
    expand.ReadIndirectBuffer(meshletCountDispatchArgs);
    expand.WriteBuffer(intermediateMeshlets);
    expand.Execute([&scene, pipelineManager, csmData, instanceMeshletOffsets, meshletCountDispatchArgs, intermediateMeshlets, instanceCount, elementCount, meshletUpperBound](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("csm_expand_meshlets"_sid);
        CSMExpandMeshletsPushConstant pc{
            .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
            .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
            .intermediateMeshlets = graph.GetBufferAddress(intermediateMeshlets),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .meshletBuffer = graph.GetBufferAddress(scene.meshlets),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .csmData = graph.GetBufferAddress(csmData),
            .instanceCount = instanceCount,
            .elementCount = elementCount,
            .currentFrameBufferMeshletLimit = meshletUpperBound,
        };
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshletCountDispatchArgs), offsetof(InstancingMeshletDispatchIndirect, x));
    });

    AddMeshletCompaction(graph, pipelineManager, cull, meshletUpperBound, scene.readback, offsetof(ReadbackStruct, shadowMeshletCount), false, PREFIX, CATEGORY);

    RenderPass& draw = graph.AddPass("[CSM] Depth"_sid, VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, CATEGORY);
    draw.WriteDepthAttachment(atlas);
    draw.ReadBuffer(csmData);
    draw.ReadBuffer(scene.models);
    draw.ReadBuffer(scene.materials);
    draw.ReadBuffer(scene.instances);
    draw.ReadBuffer(scene.primitives);
    draw.ReadBuffer(scene.meshlets);
    draw.ReadBuffer(scene.meshletVertices);
    draw.ReadBuffer(scene.meshletTriangles);
    draw.ReadBuffer(scene.vertexPositions);
    draw.ReadBuffer(scene.vertexAttributes);
    draw.ReadBuffer(visibleMeshlets);
    draw.ReadIndirectBuffer(compactedMeshletDispatchArgs);
    draw.Execute([&scene, pipelineManager, csmData, atlas, atlasExtent, visibleMeshlets, compactedMeshletDispatchArgs, slopeBias = viewFamily.csm.slopeBias](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const VkViewport viewport = VkHelpers::GenerateViewport(atlasExtent.x, atlasExtent.y);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        const VkRect2D scissor = VkHelpers::GenerateScissor(atlasExtent.x, atlasExtent.y);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        // Reverse-Z: a negative slope factor pushes depth away from the sun.
        vkCmdSetDepthBias(cmd, 0.0f, 0.0f, -slopeBias);

        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(atlas), nullptr, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({atlasExtent.x, atlasExtent.y}, nullptr, 0, &depthAttachment, nullptr);
        vkCmdBeginRendering(cmd, &renderInfo);

        CSMDepthPushConstant pc{
            .csmData = graph.GetBufferAddress(csmData),
            .vertexPosBuffer = graph.GetBufferAddress(scene.vertexPositions),
            .vertexAttrBuffer = graph.GetBufferAddress(scene.vertexAttributes),
            .meshletVerticesBuffer = graph.GetBufferAddress(scene.meshletVertices),
            .meshletTrianglesBuffer = graph.GetBufferAddress(scene.meshletTriangles),
            .meshletBuffer = graph.GetBufferAddress(scene.meshlets),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .visibleMeshlets = graph.GetBufferAddress(visibleMeshlets),
            .compactedDispatchBuffer = graph.GetBufferAddress(compactedMeshletDispatchArgs),
        };

        const PipelineEntry* regionPipelines[MESHLET_REGION_COUNT] = {
            pipelineManager->GetPipelineEntry("csm_depth"_sid),
            pipelineManager->GetPipelineEntry("csm_depth"_sid),
            pipelineManager->GetPipelineEntry("csm_depth_cutout"_sid),
            pipelineManager->GetPipelineEntry("csm_depth_cutout"_sid),
        };
        for (uint32_t region = 0; region < MESHLET_REGION_COUNT; region++) {
            const PipelineEntry* entry = regionPipelines[region];
            if (region == 0 || entry != regionPipelines[region - 1]) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, entry->pipeline);
            }
            vkCmdSetCullMode(cmd, (region & 1u) ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
            pc.drawRegion = region;
            vkCmdPushConstants(cmd, entry->layout, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(pc), &pc);
            vkCmdDrawMeshTasksIndirectEXT(cmd, graph.GetBufferHandle(compactedMeshletDispatchArgs), offsetof(InstancingCompactedMeshletDispatchIndirect, regionArgs) + region * sizeof(uint4), 1, sizeof(uint4));
        }

        vkCmdEndRendering(cmd);
    });

    return atlas;
}

SunShadowFrame SetupCSMResolve(RenderGraph& graph,
                               PipelineManager* pipelineManager,
                               Core::Extent2D renderExtent,
                               const RenderTargets& targets,
                               const SceneResources& scene,
                               RDGBuffer csmData,
                               RDGTexture atlas,
                               uint32_t sceneIndex,
                               uint64_t frameNumber)
{
    ZoneScoped;
    SunShadowFrame sunShadow{};
    if (!atlas.IsValid()) {
        return sunShadow;
    }

    sunShadow.shadow = graph.CreateTexture("csm_shadow"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);

    RenderPass& pass = graph.AddPass("[CSM] Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::ShadowMaps);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(csmData);
    pass.ReadSampledImage(targets.depthCopy);
    pass.ReadSampledImage(targets.gbufferOne);
    pass.ReadSampledImage(atlas);
    pass.WriteStorageImage(sunShadow.shadow);
    pass.Execute([&scene, pipelineManager, csmData, atlas, renderExtent, sceneIndex, frameNumber, depth = targets.depthCopy, gbufferOne = targets.gbufferOne, output = sunShadow.shadow](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("csm_resolve"_sid);
        const ResourceDimensions& atlasDims = graph.GetImageDimensions(atlas);
        CSMResolvePushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .csmData = graph.GetBufferAddress(csmData),
            .renderExtent = {renderExtent.width, renderExtent.height},
            .atlasExtent = {atlasDims.width, atlasDims.height},
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .atlasIndex = graph.GetSampledImageViewDescriptorIndex(atlas),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
        };
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (renderExtent.width + 15) / 16, (renderExtent.height + 15) / 16, 1);
    });
    return sunShadow;
}

void SetupShadowsResolve(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         Core::Extent2D renderExtent,
                         const RenderTargets& targets,
                         const SceneResources& scene,
                         const GTAOFrame& gtao,
                         uint32_t sceneIndex)
{
    ZoneScoped;
    RenderPass& shadowsResolvePass = graph.AddPass("Shadows Resolve"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::AmbientOcclusion);

    const RDGTexture gtaoFiltered = gtao.filtered;
    bool bHasGTAO = gtaoFiltered.IsValid();
    if (bHasGTAO) {
        shadowsResolvePass.ReadSampledImage(gtaoFiltered);
    }

    const float temporalMaxAccum = viewFamily.gtaoConfig.temporalMaxAccum;
    const float temporalClampScale = viewFamily.gtaoConfig.temporalClampScale;
    const bool bTemporal = bHasGTAO && temporalMaxAccum > 0.0f;
    RDGTextureRing gtaoTemporal{};
    if (bTemporal) {
        gtaoTemporal = graph.CreateVersionedTexture("gtao_temporal"_sid, TextureInfo{VK_FORMAT_R16G16_UNORM, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true, VK_IMAGE_USAGE_SAMPLED_BIT);
    }
    const bool bHistoryValid = bTemporal && gtaoTemporal.Version(1).IsValid() && targets.depthCopyHistory.IsValid() && targets.gbufferOneHistory.IsValid();
    const RDGTexture gtaoTemporalOut = gtaoTemporal.Current();
    const RDGTexture gtaoTemporalPrev = bHistoryValid ? gtaoTemporal.Version(1) : RDGTexture{};
    const RDGTexture depthHistory = targets.depthCopyHistory;
    const RDGTexture gbufferOneHistory = targets.gbufferOneHistory;
    if (bTemporal) {
        shadowsResolvePass.ReadSampledImage(targets.depthCopy);
        shadowsResolvePass.ReadSampledImage(targets.gbufferOne);
        shadowsResolvePass.WriteStorageImage(gtaoTemporalOut);
    }
    if (bHistoryValid) {
        shadowsResolvePass.ReadSampledImage(gtaoTemporalPrev);
        shadowsResolvePass.ReadSampledImage(depthHistory);
        shadowsResolvePass.ReadSampledImage(gbufferOneHistory);
    }

    shadowsResolvePass.ReadBuffer(scene.sceneData);
    shadowsResolvePass.WriteStorageImage(targets.shadows);
    shadowsResolvePass.Execute([&scene, pipelineManager, bHasGTAO, bTemporal, bHistoryValid, gtaoFiltered, gtaoTemporalOut, gtaoTemporalPrev, depthHistory, gbufferOneHistory, temporalMaxAccum,
            temporalClampScale, depth = targets.depthCopy, gbufferOne = targets.gbufferOne, output = targets.shadows,
            renderExtent, sceneIndex](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("shadows_resolve"_sid);

            int32_t gtaoIndex = bHasGTAO ? static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(gtaoFiltered)) : -1;

            ShadowsResolvePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData) + sizeof(SceneData) * sceneIndex,
                .gtaoFilteredIndex = gtaoIndex,
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .depthIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(depth) : ~0x0u,
                .gbufferOneIndex = bTemporal ? graph.GetSampledImageViewDescriptorIndex(gbufferOne) : ~0x0u,
                .historyIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(gtaoTemporalPrev) : ~0x0u,
                .depthHistoryIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0x0u,
                .gbufferOneHistoryIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0x0u,
                .temporalOutputIndex = bTemporal ? graph.GetStorageImageViewDescriptorIndex(gtaoTemporalOut) : ~0x0u,
                .bHistoryValid = bHistoryValid ? 1u : 0u,
                .temporalMaxAccum = temporalMaxAccum,
                .temporalClampScale = temporalClampScale,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t xDispatch = (renderExtent.width + 15) / 16;
            uint32_t yDispatch = (renderExtent.height + 15) / 16;
            vkCmdDispatch(cmd, xDispatch, yDispatch, 1);
        });
}

static void AddSigmaBlurPass(RenderGraph& graph,
                             PipelineManager* pipelineManager,
                             const Core::SIGMAParams& sigma,
                             Core::Extent2D renderExtent,
                             const SceneResources& scene,
                             uint32_t sceneIndex,
                             uint64_t frameNumber,
                             StringID passName,
                             RDGTexture inputTex,
                             RDGTexture outputTex,
                             RDGTexture tilesTex,
                             uint32_t passIndex,
                             RDGTexture depth,
                             RDGTexture gbufferOne)
{
    ZoneScoped;
    RenderPass& pass = graph.AddPass(passName, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadBuffer(scene.lightData);
    pass.ReadSampledImage(inputTex);
    pass.ReadSampledImage(tilesTex);
    pass.ReadSampledImage(depth);
    pass.ReadSampledImage(gbufferOne);
    pass.WriteStorageImage(outputTex);
    pass.Execute([&scene, pipelineManager, sigma, sceneIndex, renderExtent, frameNumber, passIndex,
            inputTex, outputTex, tilesTex, depth, gbufferOne](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("sigma_shadow_blur"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaBlurPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .lightData = graph.GetBufferAddress(scene.lightData),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(inputTex),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(outputTex),
                .sceneDataIndex = sceneIndex,
                .frameIndex = static_cast<uint32_t>(frameNumber),
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tilesTex),
                .passIndex = passIndex,
                .maxKernelPixels = sigma.maxKernelPixels,
                .penumbraScale = sigma.penumbraScale,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent.width + 7) / 8;
            const uint32_t groupsY = (renderExtent.height + 7) / 8;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
}

SigmaDenoiseFrame SetupSigmaShadowDenoise(RenderGraph& graph,
                                          PipelineManager* pipelineManager,
                                          const Core::ViewFamily& viewFamily,
                                          Core::Extent2D renderExtent,
                                          const RenderTargets& targets,
                                          const SceneResources& scene,
                                          SunShadowFrame& sunShadow,
                                          uint32_t sceneIndex,
                                          uint64_t frameNumber)
{
    ZoneScoped;
    if (!sunShadow.shadow.IsValid()) { return {}; }

    const Core::SIGMAParams& sigma = viewFamily.sigmaParams;
    const RDGTexture rtShadow = sunShadow.shadow;
    // Half res denoises in half-res space against the trace's aux guides; full res uses the full gbuffer.
    const RDGTexture sigmaDepth = sigma.bHalfRes ? sunShadow.depth : targets.depthCopy;
    const RDGTexture sigmaGbuffer = sigma.bHalfRes ? sunShadow.gbuffer : targets.gbufferOne;

    // R = denoised visibility, G = penumbra (world units)
    const RDGTexture sigmaShadow = graph.CreateTexture("sigma_shadow"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);

    const uint32_t tilesX = (renderExtent.width + 15) / 16;
    const uint32_t tilesY = (renderExtent.height + 15) / 16;
    const RDGTexture tiles = graph.CreateTexture("sigma_tiles"_sid, TextureInfo{VK_FORMAT_R8G8B8A8_UNORM, tilesX, tilesY, 1}, {std::nullopt}, true);
    const RDGTexture tilesSmoothed = graph.CreateTexture("sigma_tiles_smoothed"_sid, TextureInfo{VK_FORMAT_R8G8_UNORM, tilesX, tilesY, 1}, {std::nullopt}, true);

    RenderPass& classify = graph.AddPass("[SIGMA] Classify Tiles"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    classify.ReadBuffer(scene.sceneData);
    classify.ReadSampledImage(rtShadow);
    classify.ReadSampledImage(sigmaDepth);
    classify.WriteStorageImage(tiles);
    classify.Execute([&scene, pipelineManager, sigma, sceneIndex, renderExtent, tilesX, tilesY, sigmaDepth, rtShadow, tiles](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("sigma_classify_tiles"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaClassifyPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(rtShadow),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(sigmaDepth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(tiles),
                .sceneDataIndex = sceneIndex,
                .maxKernelPixels = sigma.maxKernelPixels,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, tilesX, tilesY, 1);
        });

    RenderPass& smooth = graph.AddPass("[SIGMA] Smooth Tiles"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    smooth.ReadSampledImage(tiles);
    smooth.WriteStorageImage(tilesSmoothed);
    smooth.Execute([pipelineManager, tilesX, tilesY, tiles, tilesSmoothed](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("sigma_smooth_tiles"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaSmoothTilesPushConstant pc{
                .tilesExtent = {tilesX, tilesY},
                .inputIndex = graph.GetSampledImageViewDescriptorIndex(tiles),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(tilesSmoothed),
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, (tilesX + 7) / 8, (tilesY + 7) / 8, 1);
        });

    AddSigmaBlurPass(graph, pipelineManager, sigma, renderExtent, scene, sceneIndex, frameNumber,
        "[SIGMA] Shadow Blur"_sid, rtShadow, sigmaShadow, tilesSmoothed, 0u,
        sigmaDepth, sigmaGbuffer);

    SigmaDenoiseFrame denoise{.blurred = sigmaShadow, .tilesSmoothed = tilesSmoothed};
    if (sigma.enablePostBlur) {
        denoise.blurred = graph.CreateTexture("sigma_shadow_2"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent.width, renderExtent.height, 1}, {std::nullopt}, true);
        AddSigmaBlurPass(graph, pipelineManager, sigma, renderExtent, scene, sceneIndex, frameNumber,
            "[SIGMA] Shadow Post-Blur"_sid, sigmaShadow, denoise.blurred, tilesSmoothed, 1u,
            sigmaDepth, sigmaGbuffer);
    }

    sunShadow.sigmaShadow = sigmaShadow;
    return denoise;
}

void SetupSigmaShadowTemporal(RenderGraph& graph,
                              PipelineManager* pipelineManager,
                              const Core::ViewFamily& viewFamily,
                              Core::Extent2D renderExtent,
                              const RenderTargets& targets,
                              const SceneResources& scene,
                              const SigmaDenoiseFrame& denoise,
                              SunShadowFrame& sunShadow,
                              uint32_t sceneIndex)
{
    ZoneScoped;
    if (!sunShadow.sigmaShadow.IsValid()) { return; }

    const Core::SIGMAParams& sigma = viewFamily.sigmaParams;
    const RDGTexture shadowTex = denoise.blurred;
    const RDGTexture tilesSmoothed = denoise.tilesSmoothed;
    const RDGTexture sigmaDepth = sigma.bHalfRes ? sunShadow.depth : targets.depthCopy;
    const RDGTexture sigmaGbuffer = sigma.bHalfRes ? sunShadow.gbuffer : targets.gbufferOne;

    const RDGTextureRing stabilized = graph.CreateVersionedTexture("sigma_stabilized"_sid, TextureInfo{VK_FORMAT_R16G16_SFLOAT, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true,
                                                                   VK_IMAGE_USAGE_SAMPLED_BIT);
    const RDGTextureRing historyLength = graph.CreateVersionedTexture("sigma_history_length"_sid, TextureInfo{VK_FORMAT_R32_UINT, renderExtent.width, renderExtent.height, 1}, 1, VersionSource::Fresh, true,
                                                                      VK_IMAGE_USAGE_SAMPLED_BIT);

    const bool bHasHistory = stabilized.Version(1).IsValid() && historyLength.Version(1).IsValid();
    const RDGTexture prevStabilized = bHasHistory ? stabilized.Version(1) : RDGTexture{};
    const RDGTexture prevHistoryLength = bHasHistory ? historyLength.Version(1) : RDGTexture{};
    const RDGTexture stabilizedOut = stabilized.Current();
    const RDGTexture historyLengthOut = historyLength.Current();

    RenderPass& pass = graph.AddPass("[SIGMA] Shadow Temporal"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, Render::RenderCategory::DirectionalLighting);
    pass.ReadBuffer(scene.sceneData);
    pass.ReadSampledImage(shadowTex);
    pass.ReadSampledImage(tilesSmoothed);
    pass.ReadSampledImage(sigmaDepth);
    pass.ReadSampledImage(sigmaGbuffer);
    if (bHasHistory) {
        pass.ReadSampledImage(prevStabilized);
        pass.ReadSampledImage(prevHistoryLength);
    }
    pass.WriteStorageImage(stabilizedOut);
    pass.WriteStorageImage(historyLengthOut);
    pass.Execute([&scene, pipelineManager, sigma, sceneIndex, renderExtent, bHasHistory, shadowTex, prevStabilized, prevHistoryLength, tilesSmoothed, stabilizedOut, historyLengthOut,
            depth = sigmaDepth, gbufferOne = sigmaGbuffer](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipeline = pipelineManager->GetPipelineEntry("sigma_shadow_temporal"_sid);
            if (!pipeline) { return; }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);

            SigmaTemporalPushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .renderExtent = {renderExtent.width, renderExtent.height},
                .shadowIndex = graph.GetSampledImageViewDescriptorIndex(shadowTex),
                .historyIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(prevStabilized) : ~0x0u,
                .historyLengthIndex = bHasHistory ? graph.GetSampledImageViewDescriptorIndex(prevHistoryLength) : ~0x0u,
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .outputIndex = graph.GetStorageImageViewDescriptorIndex(stabilizedOut),
                .outHistoryLengthIndex = graph.GetStorageImageViewDescriptorIndex(historyLengthOut),
                .sceneDataIndex = sceneIndex,
                .tilesIndex = graph.GetSampledImageViewDescriptorIndex(tilesSmoothed),
                .stabilizationStrength = sigma.historyWeight,
                .pixelScale = sigma.bHalfRes ? 2u : 1u,
            };
            vkCmdPushConstants(cmd, pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            const uint32_t groupsX = (renderExtent.width + 7) / 8;
            const uint32_t groupsY = (renderExtent.height + 7) / 8;
            vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });

    sunShadow.sigmaStabilized = stabilizedOut;
}
} // Render
