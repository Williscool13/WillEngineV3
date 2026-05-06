//
// Created by William on 2026-05-06.
//

#ifndef WILL_ENGINE_VK_PIPELINE_STATS_H
#define WILL_ENGINE_VK_PIPELINE_STATS_H

#include <volk.h>

#include "core/containers/array.h"
#include "render/interface/render_interface.h"

namespace Render
{
struct VulkanContext;

struct PipelineStatsResults
{
    uint64_t clippingInvocations{};
    uint64_t clippingPrimitives{};
    uint64_t fragmentInvocations{};
    uint64_t computeInvocations{};
    uint64_t meshInvocations{};
};

/**
 * One pipeline-statistics query pool per frame-in-flight. Wraps the entire frame.
 *
 * Call order each frame (render thread only):
 *   `Collect` after vkWaitForFences
 *   `Begin` after vkBeginCommandBuffer
 *   `End` before vkEndCommandBuffer
 */
struct PipelineStatsQueryPool
{
    static constexpr VkQueryPipelineStatisticFlags kBaseStatFlags =
        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |        // index 0
        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |         // index 1
        VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT | // index 2
        VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;   // index 3

    static constexpr uint32_t kMaxStatCount = 5;

    void Init(VulkanContext* context);
    void Destroy(VkDevice device);

    /**
     * Reads results from the completed frame slot and writes them into scratch.
     * @param device
     * @param frameIndex Frame-in-flight index whose fence has just been waited on.
     */
    PipelineStatsResults Collect(VkDevice device, uint32_t frameIndex);

    /**
     * @param cmd Command buffer that has been begun.
     * @param frameIndex Current frame-in-flight index.
     */
    void Begin(VkCommandBuffer cmd, uint32_t frameIndex);

    /**
     * @param cmd
     * @param frameIndex
     */
    void End(VkCommandBuffer cmd, uint32_t frameIndex);

private:
    VkQueryPipelineStatisticFlags statFlags{};
    uint32_t statCount{};
    bool bMeshShaderQueriesEnabled{};
    Core::Array<VkQueryPool, Core::FRAME_BUFFER_COUNT> pools{};
    Core::Array<bool, Core::FRAME_BUFFER_COUNT> poolRunning{};
    Core::Array<bool, Core::FRAME_BUFFER_COUNT> poolReady{};
};
} // Render

#endif //WILL_ENGINE_VK_PIPELINE_STATS_H
