//
// Created by William on 2026-04-29.
//

#include "debug_readback_buffer.h"

#include <imgui.h>

#include "render/render-graph/render_graph.h"
#include "render/vulkan/vk_context.h"

namespace Editor
{
void DebugReadbackBuffer::Init(Render::VulkanContext* context, const size_t bufferSize)
{
    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.usage = VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;
    bufferInfo.size = bufferSize;

    VmaAllocationCreateInfo vmaAllocInfo = {};
    vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    buffer = Render::AllocatedBuffer::CreateAllocatedBuffer(context, bufferInfo, vmaAllocInfo);
    buffer.SetDebugName("Debug Readback Buffer");
}

void DebugReadbackBuffer::ScheduleCopies(Render::RenderGraph& graph, const StringID name)
{
    for (const Entry& entry : entries) {
        entry.copyFn(graph, name, entry.offset);
    }
}

void DebugReadbackBuffer::Present()
{
    const auto* base = static_cast<const uint8_t*>(buffer.allocationInfo.pMappedData);

    if (ImGui::CollapsingHeader("Debug Readback")) {
        for (const Entry& entry : entries) {
            if (ImGui::TreeNode(entry.label)) {
                entry.presentFn(base + entry.offset);
                ImGui::TreePop();
            }
        }
    }
}
} // Editor
