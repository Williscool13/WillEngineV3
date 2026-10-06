//
// Created by William on 2026-07-06.
//

#include "render/passes/debug_passes.h"

#include <tracy/Tracy.hpp>

#include "render/render_utils.h"
#include "render/interface/render_interface.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"
#include "render/vulkan/vk_helpers.h"

namespace Render
{
GPUDebugFrame SetupGPUDebugBegin(RenderGraph& graph, const bool bLocked, GPUDebugLines& lines)
{
    ZoneScoped;
    GPUDebugFrame frame{};
    lines = {};
#ifdef WDEBUG
    lines.args = graph.CreateVersionedBuffer(GPU_DEBUG_ARGS_BUFFER, sizeof(GPUDebugDrawArgs), 0, graph.ResourceHasVersion(GPU_DEBUG_ARGS_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();
    lines.segments = graph.CreateVersionedBuffer(GPU_DEBUG_SEGMENT_BUFFER, GPU_DEBUG_MAX_SEGMENTS * sizeof(DebugLineSegment), 0,
                                                 graph.ResourceHasVersion(GPU_DEBUG_SEGMENT_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();
    frame.sphereArgs = graph.CreateVersionedBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER, sizeof(GPUDebugSphereArgs), 0,
                                                   graph.ResourceHasVersion(GPU_DEBUG_SPHERE_ARGS_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();
    frame.sphereInstances = graph.CreateVersionedBuffer(GPU_DEBUG_SPHERE_INSTANCE_BUFFER, GPU_DEBUG_MAX_SPHERES * sizeof(DebugSphereInstance), 0,
                                                        graph.ResourceHasVersion(GPU_DEBUG_SPHERE_INSTANCE_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();
    frame.cubeArgs = graph.CreateVersionedBuffer(GPU_DEBUG_CUBE_ARGS_BUFFER, sizeof(GPUDebugCubeArgs), 0,
                                                 graph.ResourceHasVersion(GPU_DEBUG_CUBE_ARGS_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();
    frame.cubeInstances = graph.CreateVersionedBuffer(GPU_DEBUG_CUBE_INSTANCE_BUFFER, GPU_DEBUG_MAX_CUBES * sizeof(DebugCubeInstance), 0,
                                                      graph.ResourceHasVersion(GPU_DEBUG_CUBE_INSTANCE_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh).Current();

    if (bLocked) {
        return frame;
    }

    RenderPass& clearPass = graph.AddPass("GPU Debug Clear"_sid, VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::Debug);
    clearPass.WriteTransferBuffer(lines.args);
    clearPass.WriteTransferBuffer(frame.sphereArgs);
    clearPass.WriteTransferBuffer(frame.cubeArgs);
    clearPass.Execute([args = lines.args, sphereArgsBuffer = frame.sphereArgs, cubeArgsBuffer = frame.cubeArgs](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const GPUDebugDrawArgs drawArgs{
            .groupCountX = 0,
            .groupCountY = 1,
            .groupCountZ = 1,
            .segmentCount = 0,
            .capacity = GPU_DEBUG_MAX_SEGMENTS,
        };
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(args), 0, sizeof(GPUDebugDrawArgs), &drawArgs);

        const GPUDebugSphereArgs sphereArgs{
            .vertexCount = GPU_DEBUG_SPHERE_VERTEX_COUNT,
            .instanceCount = 0,
            .firstVertex = 0,
            .firstInstance = 0,
            .capacity = GPU_DEBUG_MAX_SPHERES,
        };
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(sphereArgsBuffer), 0, sizeof(GPUDebugSphereArgs), &sphereArgs);

        const GPUDebugCubeArgs cubeArgs{
            .vertexCount = GPU_DEBUG_CUBE_VERTEX_COUNT,
            .instanceCount = 0,
            .firstVertex = 0,
            .firstInstance = 0,
            .capacity = GPU_DEBUG_MAX_CUBES,
        };
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(cubeArgsBuffer), 0, sizeof(GPUDebugCubeArgs), &cubeArgs);
    });
#endif
    return frame;
}

void SetupGPUDebugDraw(RenderGraph& graph, PipelineManager* pipelineManager, const Core::Extent2D renderExtent, const SceneResources& scene, const GPUDebugFrame& gpuDebug, const GPUDebugLines& lines,
                       const RDGTexture depthTarget, const RDGTexture targetImage, const bool bLocked)
{
    ZoneScoped;
#ifdef WDEBUG
    if (!lines.IsValid()) {
        return;
    }

    const RDGBuffer args = lines.args;
    const RDGBuffer segments = lines.segments;
    const RDGBuffer sphereArgs = gpuDebug.sphereArgs;
    const RDGBuffer sphereInstances = gpuDebug.sphereInstances;
    const RDGBuffer cubeArgs = gpuDebug.cubeArgs;
    const RDGBuffer cubeInstances = gpuDebug.cubeInstances;

    if (!bLocked) {
        RenderPass& buildIndirectPass = graph.AddPass("GPU Debug Build Indirect"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
        buildIndirectPass.ReadWriteBuffer(args);
        buildIndirectPass.ReadWriteBuffer(sphereArgs);
        buildIndirectPass.ReadWriteBuffer(cubeArgs);
        buildIndirectPass.Execute([pipelineManager, args, sphereArgs, cubeArgs](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gpu_debug_build_indirect"_sid);
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            GPUDebugBuildIndirectPushConstant pc{
                .args = graph.GetBufferAddress(args),
                .sphereArgs = graph.GetBufferAddress(sphereArgs),
                .cubeArgs = graph.GetBufferAddress(cubeArgs),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    RenderPass& drawPass = graph.AddPass("GPU Debug Draw"_sid, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, RenderCategory::Debug);
    drawPass.WriteColorAttachment(targetImage);
    const bool bHasDepth = depthTarget.IsValid();
    if (bHasDepth) {
        drawPass.ReadWriteDepthAttachment(depthTarget);
    }
    drawPass.ReadBuffer(scene.sceneData);
    drawPass.ReadBuffer(segments);
    drawPass.ReadIndirectBuffer(args);
    drawPass.ReadBuffer(sphereInstances);
    drawPass.ReadIndirectBuffer(sphereArgs);
    drawPass.ReadBuffer(cubeInstances);
    drawPass.ReadIndirectBuffer(cubeArgs);
    drawPass.Execute([&scene, pipelineManager, renderExtent, bHasDepth, depthTarget, targetImage, args, segments, sphereArgs, sphereInstances, cubeArgs,
            cubeInstances](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* linePipeline = pipelineManager->GetPipelineEntry("debug_render_gpu"_sid);
        const PipelineEntry* spherePipeline = pipelineManager->GetPipelineEntry("debug_sphere"_sid);
        const PipelineEntry* cubePipeline = pipelineManager->GetPipelineEntry("debug_cube"_sid);
        if (!linePipeline && !spherePipeline && !cubePipeline) {
            return;
        }

        VkViewport viewport = VkHelpers::GenerateViewport(renderExtent.width, renderExtent.height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(renderExtent.width, renderExtent.height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targetImage), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingInfo renderInfo;
        if (bHasDepth) {
            const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthTarget), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            renderInfo = VkHelpers::RenderingInfo({renderExtent.width, renderExtent.height}, &colorAttachment, 1, &depthAttachment, nullptr);
        }
        else {
            renderInfo = VkHelpers::RenderingInfo({renderExtent.width, renderExtent.height}, &colorAttachment, 1, nullptr, nullptr);
        }

        vkCmdBeginRendering(cmd, &renderInfo);

        if (spherePipeline) {
            GPUDebugSphereDrawPushConstant spherePush{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .instanceBuffer = graph.GetBufferAddress(sphereInstances),
                .sceneDataIndex = 0,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, spherePipeline->pipeline);
            vkCmdPushConstants(cmd, spherePipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDebugSphereDrawPushConstant), &spherePush);

            vkCmdDrawIndirect(cmd, graph.GetBufferHandle(sphereArgs), offsetof(GPUDebugSphereArgs, vertexCount), 1, sizeof(GPUDebugSphereArgs));
        }

        if (cubePipeline) {
            GPUDebugCubeDrawPushConstant cubePush{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .instanceBuffer = graph.GetBufferAddress(cubeInstances),
                .sceneDataIndex = 0,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cubePipeline->pipeline);
            vkCmdPushConstants(cmd, cubePipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDebugCubeDrawPushConstant), &cubePush);

            vkCmdDrawIndirect(cmd, graph.GetBufferHandle(cubeArgs), offsetof(GPUDebugCubeArgs, vertexCount), 1, sizeof(GPUDebugCubeArgs));
        }

        if (linePipeline) {
            GPUDebugDrawPushConstant pushConstants{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .args = graph.GetBufferAddress(args),
                .segmentBuffer = graph.GetBufferAddress(segments),
                .sceneDataIndex = 0,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline->pipeline);
            vkCmdPushConstants(cmd, linePipeline->layout, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(GPUDebugDrawPushConstant), &pushConstants);

            vkCmdDrawMeshTasksIndirectEXT(cmd, graph.GetBufferHandle(args), offsetof(GPUDebugDrawArgs, groupCountX), 1, sizeof(GPUDebugDrawArgs));
        }

        vkCmdEndRendering(cmd);
    });
#endif
}

void SetupProbePreviewSpheres(RenderGraph& graph, PipelineManager* pipelineManager, Core::Extent2D renderExtent, const SceneResources& scene, RDGTexture depthTarget, RDGTexture targetImage,
                              const Core::ViewFamily& viewFamily)
{
    ZoneScoped;
    const Core::ProbePreviewSettings settings = viewFamily.probePreviewSettings;
    if (!settings.bActive || viewFamily.probePreviews.IsEmpty() || !depthTarget.IsValid()) {
        return;
    }

    RenderPass& pass = graph.AddPass("Probe Preview Spheres"_sid, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, RenderCategory::Debug);
    pass.WriteColorAttachment(targetImage);
    pass.ReadWriteDepthAttachment(depthTarget);
    pass.ReadBuffer(scene.sceneData);
    // Arena-backed span; the frame's view family outlives graph execution
    pass.Execute([&scene, pipelineManager, settings, spheres = viewFamily.probePreviews.Data(), sphereCount = viewFamily.probePreviews.Size(), renderExtent, depthTarget,
            targetImage](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("probe_preview_sphere"_sid);
        if (!pipelineEntry) {
            return;
        }

        VkViewport viewport = VkHelpers::GenerateViewport(renderExtent.width, renderExtent.height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(renderExtent.width, renderExtent.height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targetImage), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthTarget), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({renderExtent.width, renderExtent.height}, &colorAttachment, 1, &depthAttachment, nullptr);

        vkCmdBeginRendering(cmd, &renderInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);

        // 64 stacks x 64 slices x 6, mirroring probe_preview_sphere.slang
        constexpr uint32_t PROBE_PREVIEW_SPHERE_VERTEX_COUNT = 64u * 64u * 6u;
        for (size_t i = 0; i < sphereCount; ++i) {
            ProbePreviewSpherePushConstant pc{
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .sceneDataIndex = 0,
                .cubemapIndex = spheres[i].cubemapIndex,
                .centerRadius = {spheres[i].position, settings.radius},
                .roughness = settings.roughness,
                .bIrradiance = settings.bIrradiance ? 1u : 0u,
                .radianceScale = spheres[i].radianceScale,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDraw(cmd, PROBE_PREVIEW_SPHERE_VERTEX_COUNT, 1, 0, 0);
        }

        vkCmdEndRendering(cmd);
    });
}
void SetupClusterGridDebug(RenderGraph& graph, PipelineManager* pipelineManager, const SceneResources& scene, const GPUDebugLines& lines, uint32_t sceneIndex, float clusterZNear, float clusterZFar)
{
    ZoneScoped;
#ifdef WDEBUG
    if (!lines.IsValid() || !scene.sceneData.IsValid()) {
        return;
    }

    RenderPass& pass = graph.AddPass("Cluster Grid Debug"_sid, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadWriteBuffer(lines.args);
    pass.WriteBuffer(lines.segments);
    pass.ReadBuffer(scene.sceneData);
    pass.Execute([&scene, pipelineManager, sceneIndex, clusterZNear, clusterZFar, args = lines.args, segments = lines.segments](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gpu_debug_cluster_grid"_sid);
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ClusterGridDebugPushConstant pc{
            .args = graph.GetBufferAddress(args),
            .segmentBuffer = graph.GetBufferAddress(segments),
            .sceneData = graph.GetBufferAddress(scene.sceneData),
            .zNear = clusterZNear,
            .zFar = clusterZFar,
            .sceneDataIndex = sceneIndex,
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        const uint32_t groups = (CLUSTER_COUNT + 63u) / 64u;
        vkCmdDispatch(cmd, groups, 1, 1);
    });
#endif
}

#ifdef WDEBUG
static const StringID WORLD_GRID_DEBUG_PASS[WORLD_GRID_CASCADES] = {
    "World Grid Debug 0"_sid, "World Grid Debug 1"_sid, "World Grid Debug 2"_sid, "World Grid Debug 3"_sid,
    "World Grid Debug 4"_sid, "World Grid Debug 5"_sid, "World Grid Debug 6"_sid, "World Grid Debug 7"_sid,
};

// Cascade identification tints for the all-cascades debug view (unorm RGBA, low byte = red).
static const uint32_t WORLD_GRID_CASCADE_TINT[WORLD_GRID_CASCADES] = {
    0xFFFFFFFFu, 0xFF8C8CFFu, 0xFF8CFF8Cu, 0xFFFFB399u, 0xFFFF8CFFu, 0xFF8CFFFFu, 0xFFB3FF99u, 0xFF99B3FFu,
};
#endif

void SetupWorldGridDebug(RenderGraph& graph, PipelineManager* pipelineManager, const SceneResources& scene, const GPUDebugLines& lines, uint32_t sceneIndex, int32_t debugLevel)
{
    ZoneScoped;
#ifdef WDEBUG
    if (!lines.IsValid() || !scene.sceneData.IsValid()) {
        return;
    }

    for (uint32_t level = 0; level < WORLD_GRID_CASCADES; ++level) {
        if (debugLevel >= 0 && level != static_cast<uint32_t>(debugLevel)) {
            continue;
        }

        const uint32_t packedTint = debugLevel < 0 ? WORLD_GRID_CASCADE_TINT[level] : 0xFFFFFFFFu;

        RenderPass& pass = graph.AddPass(WORLD_GRID_DEBUG_PASS[level], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
        pass.ReadWriteBuffer(lines.args);
        pass.WriteBuffer(lines.segments);
        pass.ReadBuffer(scene.sceneData);
        pass.Execute([&scene, pipelineManager, sceneIndex, level, packedTint, args = lines.args, segments = lines.segments](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry("gpu_debug_world_grid"_sid);
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            WorldGridDebugPushConstant pc{
                .args = graph.GetBufferAddress(args),
                .segmentBuffer = graph.GetBufferAddress(segments),
                .sceneData = graph.GetBufferAddress(scene.sceneData),
                .sceneDataIndex = sceneIndex,
                .level = level,
                .packedTint = packedTint,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            const uint32_t groups = (WORLD_GRID_CELLS_PER_CASCADE + 63u) / 64u;
            vkCmdDispatch(cmd, groups, 1, 1);
        });
    }
#endif
}
} // Render
