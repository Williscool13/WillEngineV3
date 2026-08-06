//
// Created by William on 2026-07-12.
//

#include "vk_gpu_timestamp_query.h"

#include "vk_context.h"
#include "vk_utils.h"

namespace Render
{
void GPUTimestampQueryPool::Init(VulkanContext* context)
{
    timestampPeriodNs = VulkanContext::deviceInfo.properties.properties.limits.timestampPeriod;

    VkQueryPoolCreateInfo info{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = 2 * kMaxPasses;

    for (uint32_t i = 0; i < Core::FRAME_BUFFER_COUNT; ++i) {
        VK_CHECK(vkCreateQueryPool(context->device, &info, nullptr, &pools[i]));
        passCount[i] = 0;
    }
}

void GPUTimestampQueryPool::Destroy(VkDevice device)
{
    for (uint32_t i = 0; i < Core::FRAME_BUFFER_COUNT; ++i) {
        if (pools[i] != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, pools[i], nullptr);
            pools[i] = VK_NULL_HANDLE;
        }
    }
}

GPUProfileSnapshot GPUTimestampQueryPool::Collect(VkDevice device, uint32_t frameIndex)
{
    GPUProfileSnapshot snapshot{};

    const uint32_t count = passCount[frameIndex];
    if (count == 0) { return snapshot; }

    Core::Array<uint64_t, 2 * kMaxPasses> raw{};
    const VkResult result = vkGetQueryPoolResults(device, pools[frameIndex], 0, 2 * count,
                                                  2 * count * sizeof(uint64_t), raw.Data(),
                                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS) { return snapshot; }

    const auto& categories = passCategories[frameIndex];
    uint64_t frameBegin = UINT64_MAX;
    uint64_t frameEnd = 0;
    for (uint32_t p = 0; p < count; ++p) {
        const uint64_t begin = raw[2 * p];
        const uint64_t end = raw[2 * p + 1];
        if (end <= begin) { continue; }

        frameBegin = begin < frameBegin ? begin : frameBegin;
        frameEnd = end > frameEnd ? end : frameEnd;

        const float passMs = static_cast<float>(end - begin) * timestampPeriodNs * 1e-6f;
        const auto mask = static_cast<uint64_t>(categories[p]);
        for (uint32_t bit = 0; bit < RENDER_CATEGORY_BIT_COUNT; ++bit) {
            if (mask & (1ull << bit)) {
                snapshot.leafMs[bit] += passMs;
            }
        }
        snapshot.totalMs += passMs;
    }

    if (frameEnd > frameBegin) {
        snapshot.spanMs = static_cast<float>(frameEnd - frameBegin) * timestampPeriodNs * 1e-6f;
    }

    for (uint32_t bit = 0; bit < RENDER_CATEGORY_BIT_COUNT; ++bit) {
        snapshot.groupMs[static_cast<uint32_t>(RENDER_CATEGORY_GROUP_OF[bit])] += snapshot.leafMs[bit];
    }

    return snapshot;
}

void GPUTimestampQueryPool::BeginFrame(VkCommandBuffer cmd, uint32_t frameIndex)
{
    vkCmdResetQueryPool(cmd, pools[frameIndex], 0, 2 * kMaxPasses);
    passCount[frameIndex] = 0;
}

void GPUTimestampQueryPool::BeginPass(VkCommandBuffer cmd, uint32_t frameIndex, RenderCategory category)
{
    const uint32_t slot = passCount[frameIndex];
    if (slot >= kMaxPasses) { return; }

    passCategories[frameIndex][slot] = category;
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, pools[frameIndex], 2 * slot);
}

void GPUTimestampQueryPool::EndPass(VkCommandBuffer cmd, uint32_t frameIndex)
{
    const uint32_t slot = passCount[frameIndex];
    if (slot >= kMaxPasses) { return; }

    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, pools[frameIndex], 2 * slot + 1);
    passCount[frameIndex] = slot + 1;
}
} // Render
