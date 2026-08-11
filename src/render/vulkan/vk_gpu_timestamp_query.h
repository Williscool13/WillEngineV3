//
// Created by William on 2026-07-12.
//

#ifndef WILL_ENGINE_VK_GPU_TIMESTAMP_QUERY_H
#define WILL_ENGINE_VK_GPU_TIMESTAMP_QUERY_H

#include <volk.h>

#include "core/containers/array.h"
#include "render/render_config.h"
#include "render/render-graph/render_graph_resources.h"

namespace Render
{
struct VulkanContext;

/**
 * Per-frame-in-flight timestamp query pool, two timestamps per render pass.
 * Call order each frame (render thread only), mirroring PipelineStatsQueryPool:
 * `Collect` after vkWaitForFences, `BeginFrame` after vkBeginCommandBuffer, `BeginPass`/`EndPass` per RenderPass.
 */
struct GPUTimestampQueryPool
{
    static constexpr uint32_t kMaxPasses = static_cast<uint32_t>(RDG_MAX_PASSES);

    void Init(VulkanContext* context);
    void Destroy(VkDevice device, const VkAllocationCallbacks* hostAllocCallbacks);

    GPUProfileSnapshot Collect(VkDevice device, uint32_t frameIndex);

    void BeginFrame(VkCommandBuffer cmd, uint32_t frameIndex);

    void BeginPass(VkCommandBuffer cmd, uint32_t frameIndex, RenderCategory category);
    void EndPass(VkCommandBuffer cmd, uint32_t frameIndex);

private:
    float timestampPeriodNs{1.0f};
    Core::Array<VkQueryPool, Core::FRAME_BUFFER_COUNT> pools{};
    Core::Array<uint32_t, Core::FRAME_BUFFER_COUNT> passCount{};
    Core::Array<Core::Array<RenderCategory, kMaxPasses>, Core::FRAME_BUFFER_COUNT> passCategories{};
};
} // Render

#endif //WILL_ENGINE_VK_GPU_TIMESTAMP_QUERY_H
