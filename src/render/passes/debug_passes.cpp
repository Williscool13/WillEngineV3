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
void SetupGPUDebugBegin(RenderGraph& graph, const bool bLocked)
{
    ZoneScoped;
#ifdef WDEBUG
    graph.CreateVersionedBuffer(GPU_DEBUG_ARGS_BUFFER, sizeof(GPUDebugDrawArgs), 0, graph.ResourceHasVersion(GPU_DEBUG_ARGS_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);
    graph.CreateVersionedBuffer(GPU_DEBUG_SEGMENT_BUFFER, GPU_DEBUG_MAX_SEGMENTS * sizeof(DebugLineSegment), 0, graph.ResourceHasVersion(GPU_DEBUG_SEGMENT_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);
    graph.CreateVersionedBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER, sizeof(GPUDebugSphereArgs), 0, graph.ResourceHasVersion(GPU_DEBUG_SPHERE_ARGS_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);
    graph.CreateVersionedBuffer(GPU_DEBUG_SPHERE_INSTANCE_BUFFER, GPU_DEBUG_MAX_SPHERES * sizeof(DebugSphereInstance), 0, graph.ResourceHasVersion(GPU_DEBUG_SPHERE_INSTANCE_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);
    graph.CreateVersionedBuffer(GPU_DEBUG_CUBE_ARGS_BUFFER, sizeof(GPUDebugCubeArgs), 0, graph.ResourceHasVersion(GPU_DEBUG_CUBE_ARGS_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);
    graph.CreateVersionedBuffer(GPU_DEBUG_CUBE_INSTANCE_BUFFER, GPU_DEBUG_MAX_CUBES * sizeof(DebugCubeInstance), 0, graph.ResourceHasVersion(GPU_DEBUG_CUBE_INSTANCE_BUFFER, 0) ? VersionSource::NoShiftReadWrite : VersionSource::Fresh);

    if (bLocked) {
        return;
    }

    RenderPass& clearPass = graph.AddPass(SID("GPU Debug Clear"), VK_PIPELINE_STAGE_2_CLEAR_BIT, RenderCategory::Debug);
    clearPass.WriteTransferBuffer(GPU_DEBUG_ARGS_BUFFER);
    clearPass.WriteTransferBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER);
    clearPass.WriteTransferBuffer(GPU_DEBUG_CUBE_ARGS_BUFFER);
    clearPass.Execute([](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const GPUDebugDrawArgs args{
            .groupCountX = 0,
            .groupCountY = 1,
            .groupCountZ = 1,
            .segmentCount = 0,
            .capacity = GPU_DEBUG_MAX_SEGMENTS,
        };
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(GPU_DEBUG_ARGS_BUFFER), 0, sizeof(GPUDebugDrawArgs), &args);

        const GPUDebugSphereArgs sphereArgs{
            .vertexCount = GPU_DEBUG_SPHERE_VERTEX_COUNT,
            .instanceCount = 0,
            .firstVertex = 0,
            .firstInstance = 0,
            .capacity = GPU_DEBUG_MAX_SPHERES,
        };
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(GPU_DEBUG_SPHERE_ARGS_BUFFER), 0, sizeof(GPUDebugSphereArgs), &sphereArgs);

        const GPUDebugCubeArgs cubeArgs{
            .vertexCount = GPU_DEBUG_CUBE_VERTEX_COUNT,
            .instanceCount = 0,
            .firstVertex = 0,
            .firstInstance = 0,
            .capacity = GPU_DEBUG_MAX_CUBES,
        };
        vkCmdUpdateBuffer(cmd, graph.GetBufferHandle(GPU_DEBUG_CUBE_ARGS_BUFFER), 0, sizeof(GPUDebugCubeArgs), &cubeArgs);
    });
#endif
}

void SetupGPUDebugDraw(RenderGraph& graph, PipelineManager* pipelineManager, const Core::Array<uint32_t, 2> renderExtent, const StringID depthTarget, const StringID targetImage, const bool bLocked)
{
    ZoneScoped;
#ifdef WDEBUG
    if (!graph.HasBuffer(GPU_DEBUG_ARGS_BUFFER)) {
        return;
    }

    if (!bLocked) {
        RenderPass& buildIndirectPass = graph.AddPass(SID("GPU Debug Build Indirect"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
        buildIndirectPass.ReadWriteBuffer(GPU_DEBUG_ARGS_BUFFER);
        buildIndirectPass.ReadWriteBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER);
        buildIndirectPass.ReadWriteBuffer(GPU_DEBUG_CUBE_ARGS_BUFFER);
        buildIndirectPass.Execute([pipelineManager](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gpu_debug_build_indirect"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            GPUDebugBuildIndirectPushConstant pc{
                .args = graph.GetBufferAddress(GPU_DEBUG_ARGS_BUFFER),
                .sphereArgs = graph.GetBufferAddress(GPU_DEBUG_SPHERE_ARGS_BUFFER),
                .cubeArgs = graph.GetBufferAddress(GPU_DEBUG_CUBE_ARGS_BUFFER),
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmd, 1, 1, 1);
        });
    }

    RenderPass& drawPass = graph.AddPass(SID("GPU Debug Draw"), VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, RenderCategory::Debug);
    drawPass.WriteColorAttachment(targetImage);
    const bool bHasDepth = graph.HasTexture(depthTarget);
    if (bHasDepth) {
        drawPass.ReadWriteDepthAttachment(depthTarget);
    }
    drawPass.ReadBuffer(SCENE_DATA_BUFFER);
    drawPass.ReadBuffer(GPU_DEBUG_SEGMENT_BUFFER);
    drawPass.ReadIndirectBuffer(GPU_DEBUG_ARGS_BUFFER);
    drawPass.ReadBuffer(GPU_DEBUG_SPHERE_INSTANCE_BUFFER);
    drawPass.ReadIndirectBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER);
    drawPass.ReadBuffer(GPU_DEBUG_CUBE_INSTANCE_BUFFER);
    drawPass.ReadIndirectBuffer(GPU_DEBUG_CUBE_ARGS_BUFFER);
    drawPass.Execute([pipelineManager, width = renderExtent[0], height = renderExtent[1], bHasDepth, depthTarget, targetImage](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* linePipeline = pipelineManager->GetPipelineEntry(SID("debug_render_gpu"));
        const PipelineEntry* spherePipeline = pipelineManager->GetPipelineEntry(SID("debug_sphere"));
        const PipelineEntry* cubePipeline = pipelineManager->GetPipelineEntry(SID("debug_cube"));
        if (!linePipeline && !spherePipeline && !cubePipeline) {
            return;
        }

        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targetImage), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingInfo renderInfo;
        if (bHasDepth) {
            const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthTarget), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            renderInfo = VkHelpers::RenderingInfo({width, height}, &colorAttachment, 1, &depthAttachment, nullptr);
        }
        else {
            renderInfo = VkHelpers::RenderingInfo({width, height}, &colorAttachment, 1, nullptr, nullptr);
        }

        vkCmdBeginRendering(cmd, &renderInfo);

        if (spherePipeline) {
            GPUDebugSphereDrawPushConstant spherePush{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .instanceBuffer = graph.GetBufferAddress(GPU_DEBUG_SPHERE_INSTANCE_BUFFER),
                .sceneDataIndex = 0,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, spherePipeline->pipeline);
            vkCmdPushConstants(cmd, spherePipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDebugSphereDrawPushConstant), &spherePush);

            vkCmdDrawIndirect(cmd, graph.GetBufferHandle(GPU_DEBUG_SPHERE_ARGS_BUFFER), offsetof(GPUDebugSphereArgs, vertexCount), 1, sizeof(GPUDebugSphereArgs));
        }

        if (cubePipeline) {
            GPUDebugCubeDrawPushConstant cubePush{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .instanceBuffer = graph.GetBufferAddress(GPU_DEBUG_CUBE_INSTANCE_BUFFER),
                .sceneDataIndex = 0,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cubePipeline->pipeline);
            vkCmdPushConstants(cmd, cubePipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDebugCubeDrawPushConstant), &cubePush);

            vkCmdDrawIndirect(cmd, graph.GetBufferHandle(GPU_DEBUG_CUBE_ARGS_BUFFER), offsetof(GPUDebugCubeArgs, vertexCount), 1, sizeof(GPUDebugCubeArgs));
        }

        if (linePipeline) {
            GPUDebugDrawPushConstant pushConstants{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .args = graph.GetBufferAddress(GPU_DEBUG_ARGS_BUFFER),
                .segmentBuffer = graph.GetBufferAddress(GPU_DEBUG_SEGMENT_BUFFER),
                .sceneDataIndex = 0,
            };

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline->pipeline);
            vkCmdPushConstants(cmd, linePipeline->layout, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(GPUDebugDrawPushConstant), &pushConstants);

            vkCmdDrawMeshTasksIndirectEXT(cmd, graph.GetBufferHandle(GPU_DEBUG_ARGS_BUFFER), offsetof(GPUDebugDrawArgs, groupCountX), 1, sizeof(GPUDebugDrawArgs));
        }

        vkCmdEndRendering(cmd);
    });
#endif
}

void SetupProbePreviewSpheres(RenderGraph& graph, PipelineManager* pipelineManager, Core::Array<uint32_t, 2> renderExtent, StringID depthTarget, StringID targetImage, const Core::ViewFamily& viewFamily)
{
    ZoneScoped;
    const Core::ProbePreviewSettings settings = viewFamily.probePreviewSettings;
    if (!settings.bActive || viewFamily.probePreviews.IsEmpty() || !graph.HasTexture(depthTarget)) {
        return;
    }

    RenderPass& pass = graph.AddPass(SID("Probe Preview Spheres"), VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, RenderCategory::Debug);
    pass.WriteColorAttachment(targetImage);
    pass.ReadWriteDepthAttachment(depthTarget);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    // Arena-backed span; the frame's view family outlives graph execution
    pass.Execute([pipelineManager, settings, spheres = viewFamily.probePreviews.Data(), sphereCount = viewFamily.probePreviews.Size(), width = renderExtent[0], height = renderExtent[1], depthTarget, targetImage](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("probe_preview_sphere"));
        if (!pipelineEntry) {
            return;
        }

        VkViewport viewport = VkHelpers::GenerateViewport(width, height);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = VkHelpers::GenerateScissor(width, height);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        const VkRenderingAttachmentInfo colorAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(targetImage), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        const VkRenderingAttachmentInfo depthAttachment = VkHelpers::RenderingAttachmentInfo(graph.GetImageViewHandle(depthTarget), nullptr, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderingInfo renderInfo = VkHelpers::RenderingInfo({width, height}, &colorAttachment, 1, &depthAttachment, nullptr);

        vkCmdBeginRendering(cmd, &renderInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineEntry->pipeline);

        // 64 stacks x 64 slices x 6, mirroring probe_preview_sphere.slang
        constexpr uint32_t PROBE_PREVIEW_SPHERE_VERTEX_COUNT = 64u * 64u * 6u;
        for (size_t i = 0; i < sphereCount; ++i) {
            ProbePreviewSpherePushConstant pc{
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
                .sceneDataIndex = 0,
                .cubemapIndex = spheres[i].cubemapIndex,
                .centerRadius = {spheres[i].position, settings.radius},
                .roughness = settings.roughness,
                .bIrradiance = settings.bIrradiance ? 1u : 0u,
            };
            vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDraw(cmd, PROBE_PREVIEW_SPHERE_VERTEX_COUNT, 1, 0, 0);
        }

        vkCmdEndRendering(cmd);
    });
}
void SetupClusterGridDebug(RenderGraph& graph, PipelineManager* pipelineManager, uint32_t sceneIndex, float clusterZNear, float clusterZFar)
{
    ZoneScoped;
#ifdef WDEBUG
    if (!graph.HasBuffer(GPU_DEBUG_ARGS_BUFFER) || !graph.HasBuffer(SCENE_DATA_BUFFER)) {
        return;
    }

    RenderPass& pass = graph.AddPass(SID("Cluster Grid Debug"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
    pass.ReadWriteBuffer(GPU_DEBUG_ARGS_BUFFER);
    pass.WriteBuffer(GPU_DEBUG_SEGMENT_BUFFER);
    pass.ReadBuffer(SCENE_DATA_BUFFER);
    pass.Execute([pipelineManager, sceneIndex, clusterZNear, clusterZFar](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gpu_debug_cluster_grid"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        ClusterGridDebugPushConstant pc{
            .args = graph.GetBufferAddress(GPU_DEBUG_ARGS_BUFFER),
            .segmentBuffer = graph.GetBufferAddress(GPU_DEBUG_SEGMENT_BUFFER),
            .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
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
    SID("World Grid Debug 0"), SID("World Grid Debug 1"), SID("World Grid Debug 2"), SID("World Grid Debug 3"),
    SID("World Grid Debug 4"), SID("World Grid Debug 5"), SID("World Grid Debug 6"), SID("World Grid Debug 7"),
};

// Cascade identification tints for the all-cascades debug view (unorm RGBA, low byte = red).
static const uint32_t WORLD_GRID_CASCADE_TINT[WORLD_GRID_CASCADES] = {
    0xFFFFFFFFu, 0xFF8C8CFFu, 0xFF8CFF8Cu, 0xFFFFB399u, 0xFFFF8CFFu, 0xFF8CFFFFu, 0xFFB3FF99u, 0xFF99B3FFu,
};
#endif

void SetupWorldGridDebug(RenderGraph& graph, PipelineManager* pipelineManager, uint32_t sceneIndex, int32_t debugLevel)
{
    ZoneScoped;
#ifdef WDEBUG
    if (!graph.HasBuffer(GPU_DEBUG_ARGS_BUFFER) || !graph.HasBuffer(SCENE_DATA_BUFFER)) {
        return;
    }

    for (uint32_t level = 0; level < WORLD_GRID_CASCADES; ++level) {
        if (debugLevel >= 0 && level != static_cast<uint32_t>(debugLevel)) {
            continue;
        }

        const uint32_t packedTint = debugLevel < 0 ? WORLD_GRID_CASCADE_TINT[level] : 0xFFFFFFFFu;

        RenderPass& pass = graph.AddPass(WORLD_GRID_DEBUG_PASS[level], VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, RenderCategory::Debug);
        pass.ReadWriteBuffer(GPU_DEBUG_ARGS_BUFFER);
        pass.WriteBuffer(GPU_DEBUG_SEGMENT_BUFFER);
        pass.ReadBuffer(SCENE_DATA_BUFFER);
        pass.Execute([pipelineManager, sceneIndex, level, packedTint](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
            const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("gpu_debug_world_grid"));
            if (!pipelineEntry) {
                return;
            }
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

            WorldGridDebugPushConstant pc{
                .args = graph.GetBufferAddress(GPU_DEBUG_ARGS_BUFFER),
                .segmentBuffer = graph.GetBufferAddress(GPU_DEBUG_SEGMENT_BUFFER),
                .sceneData = graph.GetBufferAddress(SCENE_DATA_BUFFER),
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
