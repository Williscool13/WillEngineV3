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
struct ShadowDepthDesc
{
    const char* passPrefix;
    const char* bufferPrefix;
    // Drawn into when valid, otherwise a transient atlas named atlasName is created and cleared
    RDGTexture atlas;
    // Tiles of a kept atlas cleared before drawing, placed by tileViews[tile].atlasScaleOffset
    uint32_t clearTiles;
    const ShadowViewGPU* tileViews;
    StringID atlasName;
    StringID opaquePipeline;
    StringID cutoutPipeline;
    glm::uvec2 atlasExtent;
    float slopeBias;
    size_t readbackOffset;
};

static StringID ShadowPassID(const char* prefix, const char* base)
{
    const Core::InlineString<96> name = Core::InlineString<96>::Format("%s %s", prefix, base);
    return StringID(name.c_str(), name.Size());
}

/** Culls every instance against every view in one chain and draws all views into one depth atlas. */
static RDGTexture SetupShadowViewDepth(RenderGraph& graph,
                                       PipelineManager* pipelineManager,
                                       const Core::ViewFamily& viewFamily,
                                       const MeshletCullBufferSizes& cullSizes,
                                       const SceneResources& scene,
                                       RDGBuffer views,
                                       uint32_t viewCount,
                                       const ShadowDepthDesc& desc,
                                       uint32_t sceneIndex)
{
    ZoneScoped;
    if (viewFamily.instanceCount == 0 || viewCount == 0) {
        return {};
    }

    const char* prefix = desc.passPrefix;
    constexpr RenderCategory CATEGORY = RenderCategory::ShadowMaps;
    const uint32_t instanceCount = viewFamily.instanceCount;
    const uint32_t elementCount = instanceCount * viewCount;
    const uint32_t meshletUpperBound = cullSizes.visibleMeshletUpperBound;
    const auto lodBias = static_cast<int32_t>(LOD_BIAS);

    const glm::uvec2 atlasExtent = desc.atlasExtent;
    const RDGTexture atlas = desc.atlas.IsValid() ? desc.atlas : graph.CreateTexture(desc.atlasName, TextureInfo{CSM_DEPTH_FORMAT, atlasExtent.x, atlasExtent.y, 1}, CLEAR_DEPTH_FAR);

    const MeshletCullBuffers cull = CreateMeshletCullBuffers(graph, cullSizes, desc.bufferPrefix);
    const RDGBuffer instanceMeshletOffsets = cull.instanceMeshletOffsets;
    const RDGBuffer intermediateMeshlets = cull.intermediateMeshlets;
    const RDGBuffer visibleMeshlets = cull.visibleMeshlets;
    const RDGBuffer meshletCountDispatchArgs = cull.meshletCountDispatchArgs;
    const RDGBuffer compactedMeshletDispatchArgs = cull.compactedMeshletDispatchArgs;

    AddMeshletCullClear(graph, cull, prefix, CATEGORY);

    RenderPass& instanceCull = graph.AddPass(ShadowPassID(prefix, "Instance Cull"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, CATEGORY);
    instanceCull.ReadBuffer(scene.sceneData);
    instanceCull.ReadBuffer(views);
    instanceCull.ReadBuffer(scene.primitives);
    instanceCull.ReadBuffer(scene.models);
    instanceCull.ReadBuffer(scene.instances);
    instanceCull.WriteBuffer(instanceMeshletOffsets);
    instanceCull.Execute([&scene, pipelineManager, views, instanceMeshletOffsets, instanceCount, viewCount, elementCount, sceneIndex, lodBias](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("shadow_instance_cull"_sid);
        ShadowInstanceCullPushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .views = graph.GetBufferAddress(views),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
            .instanceCount = instanceCount,
            .viewCount = viewCount,
            .sceneDataIndex = sceneIndex,
            .lodBias = lodBias,
        };
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (elementCount + INSTANCING_VISIBILITY_DISPATCH_X - 1) / INSTANCING_VISIBILITY_DISPATCH_X, 1, 1);
    });

    AddInstanceMeshletPrefixSum(graph, pipelineManager, cull, elementCount, prefix, CATEGORY);

    RenderPass& expand = graph.AddPass(ShadowPassID(prefix, "Expand Instance To Meshlet"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, CATEGORY);
    expand.ReadBuffer(views);
    expand.ReadBuffer(scene.instances);
    expand.ReadBuffer(scene.primitives);
    expand.ReadBuffer(scene.models);
    expand.ReadBuffer(scene.meshlets);
    expand.ReadBuffer(scene.materials);
    expand.ReadBuffer(instanceMeshletOffsets);
    expand.ReadIndirectBuffer(meshletCountDispatchArgs);
    expand.WriteBuffer(intermediateMeshlets);
    expand.Execute([&scene, pipelineManager, views, instanceMeshletOffsets, meshletCountDispatchArgs, intermediateMeshlets, instanceCount, elementCount, meshletUpperBound](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("shadow_expand_meshlets"_sid);
        ShadowExpandMeshletsPushConstant pc{
            .indirectDispatchBuffer = graph.GetBufferAddress(meshletCountDispatchArgs),
            .instanceMeshletOffsets = graph.GetBufferAddress(instanceMeshletOffsets),
            .intermediateMeshlets = graph.GetBufferAddress(intermediateMeshlets),
            .instanceBuffer = graph.GetBufferAddress(scene.instances),
            .primitiveBuffer = graph.GetBufferAddress(scene.primitives),
            .modelBuffer = graph.GetBufferAddress(scene.models),
            .meshletBuffer = graph.GetBufferAddress(scene.meshlets),
            .materialBuffer = graph.GetBufferAddress(scene.materials),
            .views = graph.GetBufferAddress(views),
            .instanceCount = instanceCount,
            .elementCount = elementCount,
            .currentFrameBufferMeshletLimit = meshletUpperBound,
        };
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatchIndirect(cmd, graph.GetBufferHandle(meshletCountDispatchArgs), offsetof(InstancingMeshletDispatchIndirect, x));
    });

    AddMeshletCompaction(graph, pipelineManager, cull, meshletUpperBound, scene.readback, desc.readbackOffset, false, prefix, CATEGORY);

    RenderPass& draw = graph.AddPass(ShadowPassID(prefix, "Depth"), VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, CATEGORY);
    draw.WriteDepthAttachment(atlas);
    draw.ReadBuffer(views);
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
    draw.Execute([&scene, pipelineManager, views, atlas, atlasExtent, visibleMeshlets, compactedMeshletDispatchArgs, slopeBias = desc.slopeBias, opaquePipeline = desc.opaquePipeline,
            cutoutPipeline = desc.cutoutPipeline, clearTiles = desc.clearTiles, tileViews = desc.tileViews](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const VkViewport viewport = VkHelpers::GenerateViewport(atlasExtent.x, atlasExtent.y);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        const VkRect2D scissor = VkHelpers::GenerateScissor(atlasExtent.x, atlasExtent.y);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        // Reverse-Z: a negative slope factor pushes depth away from the light.
        vkCmdSetDepthBias(cmd, 0.0f, 0.0f, -slopeBias);

        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(atlas), nullptr, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        const VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({atlasExtent.x, atlasExtent.y}, nullptr, 0, &depthAttachment, nullptr);
        vkCmdBeginRendering(cmd, &renderInfo);
        if (clearTiles != 0u) {
            VkClearRect rects[LOCAL_SHADOW_MAX_VIEWS];
            uint32_t rectCount = 0;
            for (uint32_t tile = 0; tile < LOCAL_SHADOW_MAX_VIEWS; ++tile) {
                if ((clearTiles & (1u << tile)) == 0u) { continue; }
                const glm::vec4& scaleOffset = tileViews[tile].atlasScaleOffset;
                const glm::vec2 offset = glm::vec2(scaleOffset.z, scaleOffset.w) * glm::vec2(atlasExtent) + 0.5f;
                const glm::vec2 size = glm::vec2(scaleOffset.x, scaleOffset.y) * glm::vec2(atlasExtent) + 0.5f;
                rects[rectCount++] = VkClearRect{{{static_cast<int32_t>(offset.x), static_cast<int32_t>(offset.y)}, {static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y)}}, 0, 1};
            }
            const VkClearAttachment clear{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .clearValue = CLEAR_DEPTH_FAR};
            vkCmdClearAttachments(cmd, 1, &clear, rectCount, rects);
        }

        ShadowDepthPushConstant pc{
            .views = graph.GetBufferAddress(views),
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
            pipelineManager->GetPipelineEntry(opaquePipeline),
            pipelineManager->GetPipelineEntry(opaquePipeline),
            pipelineManager->GetPipelineEntry(cutoutPipeline),
            pipelineManager->GetPipelineEntry(cutoutPipeline),
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

RDGTexture SetupCSMDepth(RenderGraph& graph,
                         PipelineManager* pipelineManager,
                         const Core::ViewFamily& viewFamily,
                         const SceneBufferSizes& bufferSizes,
                         const SceneResources& scene,
                         RDGBuffer csmData,
                         uint32_t cascadeCount,
                         uint32_t sceneIndex)
{
    const ShadowDepthDesc desc{
        .passPrefix = "[CSM]",
        .bufferPrefix = "csm_",
        .atlas = {},
        .clearTiles = 0,
        .tileViews = nullptr,
        .atlasName = "csm_atlas"_sid,
        .opaquePipeline = "csm_depth"_sid,
        .cutoutPipeline = "csm_depth_cutout"_sid,
        .atlasExtent = CSMAtlasExtent(cascadeCount, static_cast<uint32_t>(viewFamily.csm.resolution)),
        .slopeBias = viewFamily.csm.slopeBias,
        .readbackOffset = offsetof(ReadbackStruct, shadowMeshletCount),
    };
    return SetupShadowViewDepth(graph, pipelineManager, viewFamily, bufferSizes.shadowCull, scene, csmData, cascadeCount, desc, sceneIndex);
}

RDGTexture SetupLocalShadowDepth(RenderGraph& graph,
                                 PipelineManager* pipelineManager,
                                 const Core::ViewFamily& viewFamily,
                                 const SceneBufferSizes& bufferSizes,
                                 const SceneResources& scene,
                                 glm::uvec2& liveAtlasExtent,
                                 uint32_t sceneIndex)
{
    ZoneScoped;
    constexpr StringID ATLAS_NAME = "local_shadow_atlas"_sid;
    const glm::uvec2 atlasExtent = viewFamily.localShadowAtlasExtent;
    const bool bFresh = liveAtlasExtent != atlasExtent || !graph.ResourceHasVersion(ATLAS_NAME, 0);
    const RDGTexture atlas = graph.CreateVersionedTexture(ATLAS_NAME, TextureInfo{CSM_DEPTH_FORMAT, atlasExtent.x, atlasExtent.y, 1}, 0, bFresh ? VersionSource::Fresh : VersionSource::NoShiftReadWrite, false,
                                                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, false, CLEAR_DEPTH_FAR).Current();
    liveAtlasExtent = atlasExtent;

    // A fresh atlas holds nothing, so every active tile draws; otherwise only the tiles the engine marked dirty.
    const uint32_t drawTiles = bFresh ? viewFamily.localShadowActiveTiles : viewFamily.localShadowDirtyTiles & viewFamily.localShadowActiveTiles;
    if (drawTiles == 0u) {
        return atlas;
    }

    const ShadowDepthDesc desc{
        .passPrefix = "[Local Shadow]",
        .bufferPrefix = "local_shadow_",
        .atlas = atlas,
        .clearTiles = bFresh ? 0u : drawTiles,
        .tileViews = viewFamily.localShadowViews.Data(),
        .atlasName = ATLAS_NAME,
        .opaquePipeline = "local_shadow_depth"_sid,
        .cutoutPipeline = "local_shadow_depth_cutout"_sid,
        .atlasExtent = atlasExtent,
        .slopeBias = viewFamily.localShadows.slopeBias,
        .readbackOffset = offsetof(ReadbackStruct, localShadowMeshletCount),
    };

    const HostBufferMapping drawViews = graph.OpenHostBuffer("local_shadow_draw_views"_sid, LOCAL_SHADOW_MAX_VIEWS * sizeof(ShadowViewGPU));
    auto* views = static_cast<ShadowViewGPU*>(drawViews.data);
    uint32_t viewCount = 0;
    for (uint32_t tile = 0; tile < LOCAL_SHADOW_MAX_VIEWS; ++tile) {
        if ((drawTiles & (1u << tile)) == 0u) { continue; }
        const ShadowViewGPU& view = viewFamily.localShadowViews[tile];
        views[viewCount++] = view;
    }
    SetupShadowViewDepth(graph, pipelineManager, viewFamily, bufferSizes.localShadowCull, scene, drawViews.buffer, viewCount, desc, sceneIndex);
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
                               uint64_t frameNumber,
                               bool bRayTraceBeyond,
                               bool bAlphaTest)
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
    const bool bTrace = bRayTraceBeyond && scene.tlas.IsValid();
    if (bTrace) {
        pass.ReadTLASBuffer(scene.tlas);
        pass.ReadBuffer(scene.instances);
        pass.ReadBuffer(scene.primitives);
        pass.ReadBuffer(scene.materials);
        pass.ReadBuffer(scene.indices);
        pass.ReadBuffer(scene.vertexAttributes);
    }
    pass.WriteStorageImage(sunShadow.shadow);
    pass.Execute([&scene, pipelineManager, csmData, atlas, renderExtent, sceneIndex, frameNumber, bTrace, bAlphaTest, depth = targets.depthCopy, gbufferOne = targets.gbufferOne, output = sunShadow.shadow](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("csm_resolve"_sid);
        const ResourceDimensions& atlasDims = graph.GetImageDimensions(atlas);
        CSMResolvePushConstant pc{
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .csmData = graph.GetBufferAddress(csmData),
            .instanceBuffer = bTrace ? graph.GetBufferAddress(scene.instances) : 0,
            .primitiveBuffer = bTrace ? graph.GetBufferAddress(scene.primitives) : 0,
            .materialBuffer = bTrace ? graph.GetBufferAddress(scene.materials) : 0,
            .indexBuffer = bTrace ? graph.GetBufferAddress(scene.indices) : 0,
            .vertexAttrBuffer = bTrace ? graph.GetBufferAddress(scene.vertexAttributes) : 0,
            .renderExtent = {renderExtent.width, renderExtent.height},
            .atlasExtent = {atlasDims.width, atlasDims.height},
            .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
            .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
            .atlasIndex = graph.GetSampledImageViewDescriptorIndex(atlas),
            .outputIndex = graph.GetStorageImageViewDescriptorIndex(output),
            .sceneDataIndex = sceneIndex,
            .frameIndex = static_cast<uint32_t>(frameNumber),
            .tlasIndex = bTrace ? graph.GetAccelerationStructureDescriptorIndex(scene.tlas) : ~0x0u,
            .bAlphaTest = bAlphaTest ? 1u : 0u,
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
                         const SunShadowFrame& sunShadow,
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
        shadowsResolvePass.WriteStorageImage(gtaoTemporalOut);
    }
    shadowsResolvePass.ReadSampledImage(targets.depthCopy);
    shadowsResolvePass.ReadSampledImage(targets.gbufferOne);

    RDGTexture sunShadowTex = sunShadow.shadow;
    if (sunShadow.sigmaShadow.IsValid()) { sunShadowTex = sunShadow.sigmaShadow; }
    if (sunShadow.sigmaStabilized.IsValid()) { sunShadowTex = sunShadow.sigmaStabilized; }
    const bool bSunUpsample = sunShadowTex.IsValid() && sunShadow.pixelScale > 1u;
    if (sunShadowTex.IsValid()) {
        shadowsResolvePass.ReadSampledImage(sunShadowTex);
    }
    if (bSunUpsample) {
        shadowsResolvePass.ReadSampledImage(sunShadow.depth);
        shadowsResolvePass.ReadSampledImage(sunShadow.gbuffer);
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
            renderExtent, sceneIndex, sunShadowTex, bSunUpsample, sunShadowDepth = sunShadow.depth, sunShadowNormal = sunShadow.gbuffer, sunShadowExtent = sunShadow.extent,
            sunShadowPixelScale = sunShadow.pixelScale](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("shadows_resolve"_sid);

            int32_t gtaoIndex = bHasGTAO ? static_cast<int32_t>(graph.GetSampledImageViewDescriptorIndex(gtaoFiltered)) : -1;

            ShadowsResolvePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData) + sizeof(SceneData) * sceneIndex,
                .sunShadowExtent = {sunShadowExtent.width, sunShadowExtent.height},
                .gtaoFilteredIndex = gtaoIndex,
                .outputImageIndex = graph.GetStorageImageViewDescriptorIndex(output),
                .depthIndex = graph.GetSampledImageViewDescriptorIndex(depth),
                .gbufferOneIndex = graph.GetSampledImageViewDescriptorIndex(gbufferOne),
                .historyIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(gtaoTemporalPrev) : ~0x0u,
                .depthHistoryIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(depthHistory) : ~0x0u,
                .gbufferOneHistoryIndex = bHistoryValid ? graph.GetSampledImageViewDescriptorIndex(gbufferOneHistory) : ~0x0u,
                .temporalOutputIndex = bTemporal ? graph.GetStorageImageViewDescriptorIndex(gtaoTemporalOut) : ~0x0u,
                .bHistoryValid = bHistoryValid ? 1u : 0u,
                .temporalMaxAccum = temporalMaxAccum,
                .temporalClampScale = temporalClampScale,
                .sunShadowIndex = sunShadowTex.IsValid() ? graph.GetSampledImageViewDescriptorIndex(sunShadowTex) : ~0x0u,
                .sunShadowDepthIndex = bSunUpsample ? graph.GetSampledImageViewDescriptorIndex(sunShadowDepth) : ~0x0u,
                .sunShadowNormalIndex = bSunUpsample ? graph.GetSampledImageViewDescriptorIndex(sunShadowNormal) : ~0x0u,
                .sunShadowPixelScale = sunShadowPixelScale,
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
