//
// Created by William on 2026-05-06.
//

#include "vk_pipeline_stats.h"

#include "vk_context.h"
#include "vk_utils.h"
#include "render/renderer_statistics.h"

namespace Render
{
void PipelineStatsQueryPool::Init(VulkanContext* context)
{
    statFlags = kBaseStatFlags;
    statCount = 4;
    bMeshShaderQueriesEnabled = context->bMeshShaderQueriesEnabled;
    if (bMeshShaderQueriesEnabled) {
        statFlags |= VK_QUERY_PIPELINE_STATISTIC_MESH_SHADER_INVOCATIONS_BIT_EXT;
        statCount = 5;
    }

    VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
    info.queryCount = 1;
    info.pipelineStatistics = statFlags;

    for (uint32_t i = 0; i < Core::FRAME_BUFFER_COUNT; ++i) {
        VK_CHECK(vkCreateQueryPool(context->device, &info, context->HostAllocCallbacks(), &pools[i]));
    }
}

void PipelineStatsQueryPool::Destroy(VkDevice device, const VkAllocationCallbacks* hostAllocCallbacks)
{
    for (uint32_t i = 0; i < Core::FRAME_BUFFER_COUNT; ++i) {
        if (pools[i] != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, pools[i], hostAllocCallbacks);
            pools[i] = VK_NULL_HANDLE;
        }
    }
}

PipelineStatsResults PipelineStatsQueryPool::Collect(VkDevice device, uint32_t frameIndex)
{
    PipelineStatsResults pipelineStats{};

    if (!poolReady[frameIndex]) { return pipelineStats; }
    Core::Array<uint64_t, kMaxStatCount + 1> raw{};
    vkGetQueryPoolResults(device, pools[frameIndex], 0, 1,
                          (statCount + 1) * sizeof(uint64_t), raw.Data(),
                          sizeof(uint64_t),
                          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    if (raw[statCount]) {
        pipelineStats.clippingInvocations = raw[0];
        pipelineStats.clippingPrimitives = raw[1];
        pipelineStats.fragmentInvocations = raw[2];
        pipelineStats.computeInvocations = raw[3];
        if (bMeshShaderQueriesEnabled) {
            pipelineStats.meshInvocations = raw[4];
        }
    }

    return pipelineStats;
}

void PipelineStatsQueryPool::Begin(VkCommandBuffer cmd, uint32_t frameIndex)
{
    vkCmdResetQueryPool(cmd, pools[frameIndex], 0, 1);
    vkCmdBeginQuery(cmd, pools[frameIndex], 0, 0);

    poolRunning[frameIndex] = true;
}

void PipelineStatsQueryPool::End(VkCommandBuffer cmd, uint32_t frameIndex)
{
    if (!poolRunning[frameIndex]) {
        return;
    }
    vkCmdEndQuery(cmd, pools[frameIndex], 0);
    poolReady[frameIndex] = true;
    poolRunning[frameIndex] = false;
}
} // Render
