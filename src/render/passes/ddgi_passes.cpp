//
// Created by William on 2026-07-06.
//

#include "render/passes/ddgi_passes.h"

#include "render/render_utils.h"
#include "render/pipelines/pipeline_data.h"
#include "render/pipelines/pipeline_manager.h"
#include "render/render-graph/render_pass.h"

namespace Render
{
DDGIVolumeParams ComputeDDGIVolumeParams(const glm::vec3& cameraPosition)
{
    const glm::ivec3 counts{DDGI_PROBE_COUNT_X, DDGI_PROBE_COUNT_Y, DDGI_PROBE_COUNT_Z};
    const glm::ivec3 centerCell = glm::ivec3(glm::floor(cameraPosition / DDGI_PROBE_SPACING + 0.5f));

    DDGIVolumeParams params{};
    params.baseCell = centerCell - counts / 2;
    params.probeCount = glm::uvec3(counts);
    params.probeSpacing = glm::vec3(DDGI_PROBE_SPACING);
    return params;
}

void SetupDDGIProbeDebug(RenderGraph& graph, PipelineManager* pipelineManager, const DDGIVolumeParams& volume)
{
#ifdef WDEBUG
    if (!graph.HasBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER)) {
        return;
    }

    RenderPass& pass = graph.AddPass(SID("DDGI Probe Debug"), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, ResourceCategory::Debug);
    pass.ReadWriteBuffer(GPU_DEBUG_SPHERE_ARGS_BUFFER);
    pass.WriteBuffer(GPU_DEBUG_SPHERE_INSTANCE_BUFFER);
    pass.Execute([pipelineManager, volume](VkCommandBuffer cmd, VulkanContext*, RenderGraph& graph) {
        const PipelineEntry* pipelineEntry = pipelineManager->GetPipelineEntry(SID("ddgi_probe_debug"));
        if (!pipelineEntry) {
            return;
        }
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineEntry->pipeline);

        DDGIProbeDebugPushConstant pc{
            .volume = volume,
            .sphereArgs = graph.GetBufferAddress(GPU_DEBUG_SPHERE_ARGS_BUFFER),
            .sphereBuffer = graph.GetBufferAddress(GPU_DEBUG_SPHERE_INSTANCE_BUFFER),
        };
        vkCmdPushConstants(cmd, pipelineEntry->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(cmd, (volume.probeCount.x + 3) / 4, (volume.probeCount.y + 3) / 4, (volume.probeCount.z + 3) / 4);
    });
#endif
}
} // Render
